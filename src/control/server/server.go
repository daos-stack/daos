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
	"strings"
	"syscall"
	"time"

	"github.com/pkg/errors"
	"google.golang.org/grpc"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	// ControlPlaneName defines a consistent name for the control plane server.
	ControlPlaneName = "DAOS Control Server"
	// DataPlaneName defines a consistent name for the ioserver.
	DataPlaneName = "DAOS I/O Server"
	// define supported maximum number of I/O servers
	maxIOServers = 2

	iommuPath        = "/sys/class/iommu"
	minHugePageCount = 128
)

var ioserverShutdownTimeout = 15 * time.Second

func cfgHasBdev(cfg *Configuration) bool {
	for _, srvCfg := range cfg.Servers {
		if len(srvCfg.Storage.Bdev.DeviceList) > 0 {
			return true
		}
	}

	return false
}

func instanceShmID(idx int) int {
	return os.Getpid() + idx + 1
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

// Start is the entry point for a daos_server instance.
func Start(log *logging.LeveledLogger, cfg *Configuration) error {
	err := cfg.Validate(log)
	if err != nil {
		return errors.Wrapf(err, "%s: validation failed", cfg.Path)
	}

	// Backup active config.
	saveActiveConfig(log, cfg)

	if cfg.HelperLogFile != "" {
		if err := os.Setenv(pbin.DaosAdminLogFileEnvVar, cfg.HelperLogFile); err != nil {
			return errors.Wrap(err, "unable to configure privileged helper logging")
		}
	}

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

	// Perform an automatic prepare based on the values in the config file.
	prepReq := bdev.PrepareRequest{
		// Default to minimum necessary for scan to work correctly.
		HugePageCount: minHugePageCount,
		TargetUser:    runningUser.Username,
		PCIWhitelist:  strings.Join(cfg.BdevInclude, ","),
	}

	if cfgHasBdev(cfg) {
		// The config value is intended to be per-ioserver, so we need to adjust
		// based on the number of ioservers.
		prepReq.HugePageCount = cfg.NrHugepages * len(cfg.Servers)
	}

	log.Debugf("automatic NVMe prepare req: %+v", prepReq)
	if _, err := bdevProvider.Prepare(prepReq); err != nil {
		log.Errorf("automatic NVMe prepare failed (check configuration?)\n%s", err)
	}

	hugePages, err := getHugePageInfo()
	if err != nil {
		return errors.Wrap(err, "unable to read system hugepage info")
	}

	// Don't bother with these checks if there aren't any block devices configured.
	if cfgHasBdev(cfg) {
		if hugePages.Free < prepReq.HugePageCount {
			return FaultInsufficientFreeHugePages(hugePages.Free, prepReq.HugePageCount)
		}

		if runningUser.Uid != "0" && !iommuDetected() {
			return FaultIommuDisabled
		}
	}

	// If this daos_server instance ends up being the MS leader,
	// this will record the DAOS system membership.
	membership := system.NewMembership(log)
	scmProvider := scm.DefaultProvider(log)
	harness := NewIOServerHarness(log)
	for i, srvCfg := range cfg.Servers {
		if i+1 > maxIOServers {
			break
		}

		// Provide special handling for the ofi+verbs provider.
		// Mercury uses the interface name such as ib0, while OFI uses the device name such as hfi1_0
		// CaRT and Mercury will now support the new OFI_DOMAIN environment variable so that we can
		// specify the correct device for each.
		if strings.HasPrefix(srvCfg.Fabric.Provider, "ofi+verbs") && !srvCfg.HasEnvVar("OFI_DOMAIN") {
			deviceAlias, err := netdetect.GetDeviceAlias(srvCfg.Fabric.Interface)
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

		// Each instance must have a unique shmid in order to run as SPDK primary.
		// Use a stable identifier that's easy to construct elsewhere if we don't
		// have access to the instance configuration.
		srvCfg.Storage.Bdev.ShmID = instanceShmID(i)

		bp, err := bdev.NewClassProvider(log, srvCfg.Storage.SCM.MountPoint, &srvCfg.Storage.Bdev)
		if err != nil {
			return err
		}

		msClient := newMgmtSvcClient(ctx, log, mgmtSvcClientCfg{
			AccessPoints:    cfg.AccessPoints,
			ControlAddr:     controlAddr,
			TransportConfig: cfg.TransportConfig,
		})

		srv := NewIOServerInstance(log, bp, scmProvider, msClient, ioserver.NewRunner(log, srvCfg))
		if err := harness.AddInstance(srv); err != nil {
			return err
		}
	}

	// Create and setup control service.
	controlService, err := NewControlService(log, harness, bdevProvider, scmProvider, cfg, membership)
	if err != nil {
		return errors.Wrap(err, "init control service")
	}
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
	mgmtpb.RegisterMgmtSvcServer(grpcServer, newMgmtSvc(harness, membership))

	go func() {
		_ = grpcServer.Serve(lis)
	}()
	defer grpcServer.GracefulStop()

	log.Infof("%s (pid %d) listening on %s", ControlPlaneName, os.Getpid(), controlAddr)

	sigChan := make(chan os.Signal)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGQUIT, syscall.SIGTERM)
	go func() {
		var err error
		sig := <-sigChan
		log.Debugf("Caught signal: %s", sig)

		defer func() {
			if errors.Cause(err) == context.DeadlineExceeded {
				log.Debug("resorting to kill signal")
			}
			shutdown() // Kill I/O servers if running after graceful shutdown.
		}()

		stopCtx, cancel := context.WithTimeout(ctx, ioserverShutdownTimeout)
		defer cancel()

		// Attampt graceful shutdown of I/O servers.
		if _, err = harness.StopInstances(stopCtx, sig); err != nil {
			log.Error(errors.Wrap(err, "graceful shutdown").Error())
		}
	}()

	if err := harness.AwaitStorageReady(ctx, cfg.RecreateSuperblocks); err != nil {
		return err
	}

	if err := harness.CreateSuperblocks(cfg.RecreateSuperblocks); err != nil {
		return err
	}

	return errors.Wrapf(harness.Start(ctx, membership, cfg), "%s exited with error", DataPlaneName)
}
