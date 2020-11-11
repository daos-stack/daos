//
// (C) Copyright 2018-2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package server

import (
	"context"
	"fmt"
	"io/ioutil"
	"net"
	"os"
	"os/signal"
	"os/user"
	"path/filepath"
	"strings"
	"sync"
	"syscall"

	"github.com/pkg/errors"
	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/build"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	iommuPath        = "/sys/class/iommu"
	minHugePageCount = 128
)

func cfgHasBdev(cfg *config.Server) bool {
	for _, srvCfg := range cfg.Servers {
		if len(srvCfg.Storage.Bdev.DeviceList) > 0 {
			return true
		}
	}

	return false
}

func iommuDetected() bool {
	// Simple test for now -- if the path exists and contains
	// DMAR entries, we assume that's good enough.
	dmars, err := ioutil.ReadDir(iommuPath)
	if err != nil {
		return false
	}

	return len(dmars) > 0
}

func raftDir(cfg *config.Server) string {
	if len(cfg.Servers) == 0 {
		return "" // can't save to SCM
	}
	return filepath.Join(cfg.Servers[0].Storage.SCM.MountPoint, "control_raft")
}

func hostname() string {
	hn, err := os.Hostname()
	if err != nil {
		return fmt.Sprintf("Hostname() failed: %s", err.Error())
	}
	return hn
}

// Start is the entry point for a daos_server instance.
func Start(log *logging.LeveledLogger, cfg *config.Server) error {
	err := cfg.Validate(log)
	if err != nil {
		return errors.Wrapf(err, "%s: validation failed", cfg.Path)
	}

	// Backup active config.
	config.SaveActiveConfig(log, cfg)

	if cfg.HelperLogFile != "" {
		if err := os.Setenv(pbin.DaosAdminLogFileEnvVar, cfg.HelperLogFile); err != nil {
			return errors.Wrap(err, "unable to configure privileged helper logging")
		}
	}

	if cfg.FWHelperLogFile != "" {
		if err := os.Setenv(pbin.DaosFWLogFileEnvVar, cfg.FWHelperLogFile); err != nil {
			return errors.Wrap(err, "unable to configure privileged firmware helper logging")
		}
	}

	faultDomain, err := getFaultDomain(cfg)
	if err != nil {
		return err
	}
	log.Debugf("fault domain: %s", faultDomain.String())

	// Create the root context here. All contexts should
	// inherit from this one so that they can be shut down
	// from one place.
	ctx, shutdown := context.WithCancel(context.Background())
	defer shutdown()

	controlAddr, err := net.ResolveTCPAddr("tcp", fmt.Sprintf("0.0.0.0:%d", cfg.ControlPort))
	if err != nil {
		return errors.Wrap(err, "unable to resolve daos_server control address")
	}

	bdevProvider := bdev.DefaultProvider(log)
	runningUser, err := user.Current()
	if err != nil {
		return errors.Wrap(err, "unable to lookup current user")
	}

	iommuDisabled := !iommuDetected()
	// Perform an automatic prepare based on the values in the config file.
	prepReq := bdev.PrepareRequest{
		// Default to minimum necessary for scan to work correctly.
		HugePageCount: minHugePageCount,
		TargetUser:    runningUser.Username,
		PCIWhitelist:  strings.Join(cfg.BdevInclude, " "),
		PCIBlacklist:  strings.Join(cfg.BdevExclude, " "),
		DisableVFIO:   cfg.DisableVFIO,
		DisableVMD:    cfg.DisableVMD || cfg.DisableVFIO || iommuDisabled,
		// TODO: pass vmd include/white list
	}

	if cfgHasBdev(cfg) {
		// The config value is intended to be per-ioserver, so we need to adjust
		// based on the number of ioservers.
		prepReq.HugePageCount = cfg.NrHugepages * len(cfg.Servers)

		// Perform these checks to avoid even trying a prepare if the system
		// isn't configured properly.
		if runningUser.Uid != "0" {
			if cfg.DisableVFIO {
				return FaultVfioDisabled
			}

			if iommuDisabled {
				return FaultIommuDisabled
			}
		}
	}

	log.Debugf("automatic NVMe prepare req: %+v", prepReq)
	if _, err := bdevProvider.Prepare(prepReq); err != nil {
		log.Errorf("automatic NVMe prepare failed (check configuration?)\n%s", err)
	}

	hugePages, err := getHugePageInfo()
	if err != nil {
		return errors.Wrap(err, "unable to read system hugepage info")
	}

	if cfgHasBdev(cfg) {
		// Double-check that we got the requested number of huge pages after prepare.
		if hugePages.Free < prepReq.HugePageCount {
			return FaultInsufficientFreeHugePages(hugePages.Free, prepReq.HugePageCount)
		}
	}

	// If this daos_server instance ends up being the MS leader,
	// this will record the DAOS system membership.
	sysdb := system.NewDatabase(log, &system.DatabaseConfig{
		Replicas: cfg.AccessPoints,
		RaftDir:  raftDir(cfg),
	})
	membership := system.NewMembership(log, sysdb)
	scmProvider := scm.DefaultProvider(log)
	harness := NewIOServerHarness(log).WithFaultDomain(faultDomain)
	mgmtSvc := newMgmtSvc(harness, membership, sysdb)
	var netDevClass uint32

	netCtx, err := netdetect.Init(context.Background())
	if err != nil {
		return err
	}
	defer netdetect.CleanUp(netCtx)

	// On a NUMA-aware system, emit a message when the configuration
	// may be sub-optimal.
	numaCount := netdetect.NumNumaNodes(netCtx)
	if numaCount > 0 && len(cfg.Servers) > numaCount {
		log.Infof("NOTICE: Detected %d NUMA node(s); %d-server config may not perform as expected", numaCount, len(cfg.Servers))
	}

	for idx, srvCfg := range cfg.Servers {
		// Provide special handling for the ofi+verbs provider.
		// Mercury uses the interface name such as ib0, while OFI uses the device name such as hfi1_0
		// CaRT and Mercury will now support the new OFI_DOMAIN environment variable so that we can
		// specify the correct device for each.
		if strings.HasPrefix(srvCfg.Fabric.Provider, "ofi+verbs") && !srvCfg.HasEnvVar("OFI_DOMAIN") {
			deviceAlias, err := netdetect.GetDeviceAlias(netCtx, srvCfg.Fabric.Interface)
			if err != nil {
				return errors.Wrapf(err, "failed to resolve alias for %s", srvCfg.Fabric.Interface)
			}
			envVar := "OFI_DOMAIN=" + deviceAlias
			srvCfg.WithEnvVars(envVar)
		}

		// If the configuration specifies that we should explicitly set hugepage values
		// per instance, do it. Otherwise, let SPDK/DPDK figure it out.
		if cfg.SetHugepages {
			// If we have multiple I/O instances with block devices, then we need to apportion
			// the hugepage memory among the instances.
			srvCfg.Storage.Bdev.MemSize = hugePages.FreeMB() / len(cfg.Servers)
			// reserve a little for daos_admin
			srvCfg.Storage.Bdev.MemSize -= srvCfg.Storage.Bdev.MemSize / 16
		}

		// Indicate whether VMD devices have been detected and can be used.
		srvCfg.Storage.Bdev.VmdDisabled = bdevProvider.IsVMDDisabled()

		bp, err := bdev.NewClassProvider(log, srvCfg.Storage.SCM.MountPoint, &srvCfg.Storage.Bdev)
		if err != nil {
			return err
		}

		msClient := newMgmtSvcClient(ctx, log, mgmtSvcClientCfg{
			AccessPoints:    cfg.AccessPoints,
			ControlAddr:     controlAddr,
			TransportConfig: cfg.TransportConfig,
		})

		srv := NewIOServerInstance(log, bp, scmProvider, msClient, ioserver.NewRunner(log, srvCfg)).
			WithHostFaultDomain(faultDomain)
		if err := harness.AddInstance(srv); err != nil {
			return err
		}

		if idx == 0 {
			netDevClass, err = cfg.GetDeviceClassFn(srvCfg.Fabric.Interface)
			if err != nil {
				return err
			}

			// Start the system db after instance 0's SCM is
			// ready.
			var once sync.Once
			srv.OnStorageReady(func(ctx context.Context) (err error) {
				once.Do(func() {
					err = errors.Wrap(sysdb.Start(ctx, controlAddr),
						"failed to start system db",
					)
				})
				return
			})
		}
	}

	// Create rpcClient for inter-server communication.
	cliCfg := control.DefaultConfig()
	cliCfg.TransportConfig = cfg.TransportConfig
	rpcClient := control.NewClient(
		control.WithConfig(cliCfg),
		control.WithClientLogger(log))

	// Create and setup control service.
	controlService := NewControlService(log, harness, bdevProvider, scmProvider, cfg, membership, rpcClient)
	if err := controlService.Setup(); err != nil {
		return errors.Wrap(err, "setup control service")
	}

	// Create and start listener on management network.
	lis, err := net.Listen("tcp4", controlAddr.String())
	if err != nil {
		return errors.Wrap(err, "unable to listen on management interface")
	}

	// Create new grpc server, register services and start serving.
	unaryInterceptors := []grpc.UnaryServerInterceptor{
		unaryErrorInterceptor,
		unaryStatusInterceptor,
	}
	streamInterceptors := []grpc.StreamServerInterceptor{
		streamErrorInterceptor,
	}
	opts := []grpc.ServerOption{}
	tcOpt, err := security.ServerOptionForTransportConfig(cfg.TransportConfig)
	if err != nil {
		return err
	}
	opts = append(opts, tcOpt)

	uintOpt, err := unaryInterceptorForTransportConfig(cfg.TransportConfig)
	if err != nil {
		return err
	}
	if uintOpt != nil {
		unaryInterceptors = append(unaryInterceptors, uintOpt)
	}
	sintOpt, err := streamInterceptorForTransportConfig(cfg.TransportConfig)
	if err != nil {
		return err
	}
	if sintOpt != nil {
		streamInterceptors = append(streamInterceptors, sintOpt)
	}
	opts = append(opts, []grpc.ServerOption{
		grpc.ChainUnaryInterceptor(unaryInterceptors...),
		grpc.ChainStreamInterceptor(streamInterceptors...),
	}...)

	grpcServer := grpc.NewServer(opts...)
	ctlpb.RegisterMgmtCtlServer(grpcServer, controlService)

	mgmtSvc.clientNetworkCfg = &config.ClientNetworkCfg{
		Provider:        cfg.Fabric.Provider,
		CrtCtxShareAddr: cfg.Fabric.CrtCtxShareAddr,
		CrtTimeout:      cfg.Fabric.CrtTimeout,
		NetDevClass:     netDevClass,
	}
	mgmtpb.RegisterMgmtSvcServer(grpcServer, mgmtSvc)
	sysdb.OnLeadershipGained(func(ctx context.Context) error {
		log.Infof("MS leader running on %s", hostname())
		mgmtSvc.startJoinLoop(ctx)
		return nil
	})
	sysdb.OnLeadershipLost(func() error {
		log.Infof("MS leader no longer running on %s", hostname())
		return nil
	})

	go func() {
		_ = grpcServer.Serve(lis)
	}()
	defer grpcServer.GracefulStop()

	log.Infof("%s (pid %d) listening on %s", build.ControlPlaneName, os.Getpid(), controlAddr)

	sigChan := make(chan os.Signal)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGQUIT, syscall.SIGTERM)
	go func() {
		// SIGKILL I/O servers immediately on exit.
		// TODO: Re-enable attempted graceful shutdown of I/O servers.
		sig := <-sigChan
		log.Debugf("Caught signal: %s", sig)

		shutdown()
	}()

	return errors.Wrapf(harness.Start(ctx, membership, sysdb, cfg), "%s exited with error", build.DataPlaneName)
}
