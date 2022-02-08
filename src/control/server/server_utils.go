//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"io/ioutil"
	"net"
	"path/filepath"
	"strconv"
	"strings"
	"sync"

	"github.com/pkg/errors"
	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

// netListenerFn is a type alias for the net.Listener function signature.
type netListenFn func(string, string) (net.Listener, error)

// resolveTCPFn is a type alias for the net.ResolveTCPAddr function signature.
type resolveTCPFn func(string, string) (*net.TCPAddr, error)

const (
	iommuPath        = "/sys/class/iommu"
	minHugePageCount = 128
)

func cfgHasBdevs(cfg *config.Server) bool {
	for _, engineCfg := range cfg.Engines {
		for _, bc := range engineCfg.Storage.Tiers.BdevConfigs() {
			if bc.Bdev.DeviceList.Len() > 0 {
				return true
			}
		}
	}

	return false
}

func cfgGetReplicas(cfg *config.Server, resolver resolveTCPFn) ([]*net.TCPAddr, error) {
	var dbReplicas []*net.TCPAddr
	for _, ap := range cfg.AccessPoints {
		apAddr, err := resolver("tcp", ap)
		if err != nil {
			return nil, config.FaultConfigBadAccessPoints
		}
		dbReplicas = append(dbReplicas, apAddr)
	}

	return dbReplicas, nil
}

func cfgGetRaftDir(cfg *config.Server) string {
	if len(cfg.Engines) == 0 {
		return "" // can't save to SCM
	}
	if len(cfg.Engines[0].Storage.Tiers.ScmConfigs()) == 0 {
		return ""
	}

	return filepath.Join(cfg.Engines[0].Storage.Tiers.ScmConfigs()[0].Scm.MountPoint, "control_raft")
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

func createListener(ctlPort int, resolver resolveTCPFn, listener netListenFn) (*net.TCPAddr, net.Listener, error) {
	ctlAddr, err := resolver("tcp", fmt.Sprintf("0.0.0.0:%d", ctlPort))
	if err != nil {
		return nil, nil, errors.Wrap(err, "unable to resolve daos_server control address")
	}

	// Create and start listener on management network.
	lis, err := listener("tcp4", ctlAddr.String())
	if err != nil {
		return nil, nil, errors.Wrap(err, "unable to listen on management interface")
	}

	return ctlAddr, lis, nil
}

// updateFabricEnvars adjusts the engine fabric configuration.
func updateFabricEnvars(ctx context.Context, log logging.Logger, cfg *engine.Config, fis *hardware.FabricInterfaceSet) error {
	// In the case of some providers, mercury uses the interface name
	// such as ib0, while OFI uses the device name such as hfi1_0 CaRT and
	// Mercury will now support the new OFI_DOMAIN environment variable so
	// that we can specify the correct device for each.
	if !cfg.HasEnvVar("OFI_DOMAIN") {
		fi, err := fis.GetInterfaceOnOSDevice(cfg.Fabric.Interface, cfg.Fabric.Provider)
		if err != nil {
			return errors.Wrapf(err, "unable to determine device domain for %s", cfg.Fabric.Interface)
		}
		domain := fi.Name
		log.Debugf("setting OFI_DOMAIN=%s for %s", domain, cfg.Fabric.Interface)
		envVar := "OFI_DOMAIN=" + domain
		cfg.WithEnvVars(envVar)
	}

	return nil
}

func getFabricNetDevClass(cfg *config.Server, fis *hardware.FabricInterfaceSet) (hardware.NetDevClass, error) {
	var netDevClass hardware.NetDevClass
	for index, engine := range cfg.Engines {
		fi, err := fis.GetInterface(engine.Fabric.Interface)
		if err != nil {
			return 0, err
		}

		ndc := fi.DeviceClass
		if index == 0 {
			netDevClass = ndc
			continue
		}
		if ndc != netDevClass {
			return 0, config.FaultConfigInvalidNetDevClass(index, netDevClass,
				ndc, engine.Fabric.Interface)
		}
	}
	return netDevClass, nil
}

func prepBdevStorage(srv *server, iommuEnabled bool, hpiGetter common.GetHugePageInfoFn) error {
	// Perform an automatic prepare based on the values in the config file.
	prepReq := storage.BdevPrepareRequest{
		// Default to minimum necessary for scan to work correctly.
		HugePageCount: minHugePageCount,
		TargetUser:    srv.runningUser,
		PCIAllowList:  strings.Join(srv.cfg.BdevInclude, storage.BdevPciAddrSep),
		PCIBlockList:  strings.Join(srv.cfg.BdevExclude, storage.BdevPciAddrSep),
		DisableVFIO:   srv.cfg.DisableVFIO,
		EnableVMD:     srv.cfg.EnableVMD && !srv.cfg.DisableVFIO && iommuEnabled,
		Reset_:        true, // first reset allocations before preparing devices
	}

	hasBdevs := cfgHasBdevs(srv.cfg)
	// Use default value
	if srv.cfg.NrHugepages < 0 {
		srv.cfg.NrHugepages = 4096
	}
	// The config value is intended to be per-engine, so we need to adjust
	// based on the number of engines.
	if srv.cfg.NrHugepages > 0 {
		if len(srv.cfg.Engines) == 0 {
			prepReq.HugePageCount = srv.cfg.NrHugepages
		} else {
			prepReq.HugePageCount = srv.cfg.NrHugepages * len(srv.cfg.Engines)
		}
	}
	if hasBdevs {
		// Perform these checks to avoid even trying a prepare if the system
		// isn't configured properly.
		if srv.runningUser != "root" {
			if srv.cfg.DisableVFIO {
				return FaultVfioDisabled
			}

			if !iommuEnabled {
				return FaultIommuDisabled
			}
		}
	}

	// Run prepare with reset first to release resources.
	//
	// TODO: should be passing root context into prepare request to
	//       facilitate cancellation.
	prepReq.Reset_ = true
	if _, err := srv.ctlSvc.NvmePrepare(prepReq); err != nil {
		srv.log.Errorf("automatic NVMe prepare reset failed: %s", err)
	} else {
		prepReq.Reset_ = false
		if _, err := srv.ctlSvc.NvmePrepare(prepReq); err != nil {
			srv.log.Errorf("automatic NVMe prepare failed: %s", err)
		}
	}

	hugePages, err := hpiGetter()
	if err != nil {
		return errors.Wrap(err, "unable to read system hugepage info")
	}

	// Double-check that we got the requested number of huge pages after prepare.
	if srv.cfg.NrHugepages > 0 && hugePages.Free < prepReq.HugePageCount {
		return FaultInsufficientFreeHugePages(hugePages.Free, prepReq.HugePageCount)
	}

	for _, engineCfg := range srv.cfg.Engines {
		// Calculate mem_size per I/O engine (in MB)
		PageSizeMb := hugePages.PageSizeKb >> 10
		engineCfg.MemSize = srv.cfg.NrHugepages
		engineCfg.MemSize *= PageSizeMb
		// Pass hugepage size, do not assume 2MB is used
		engineCfg.HugePageSz = PageSizeMb
		srv.log.Debugf("MemSize:%dMB, HugepageSize:%dMB", engineCfg.MemSize, engineCfg.HugePageSz)
		// Warn if hugepages are not enough to sustain average
		// I/O workload (~1GB), ignore warning if using SCM backend with 0 hugepages
		if !hasBdevs && engineCfg.MemSize == 0 {
			continue
		}
		if (engineCfg.MemSize / engineCfg.TargetCount) < 1024 {
			srv.log.Errorf("Not enough hugepages are allocated!")
		}
	}

	return nil
}

func setDaosHelperEnvs(cfg *config.Server, setenv func(k, v string) error) error {
	if cfg.HelperLogFile != "" {
		if err := setenv(pbin.DaosAdminLogFileEnvVar, cfg.HelperLogFile); err != nil {
			return errors.Wrap(err, "unable to configure privileged helper logging")
		}
	}

	if cfg.FWHelperLogFile != "" {
		if err := setenv(pbin.DaosFWLogFileEnvVar, cfg.FWHelperLogFile); err != nil {
			return errors.Wrap(err, "unable to configure privileged firmware helper logging")
		}
	}

	return nil
}

func registerEngineEventCallbacks(engine *EngineInstance, hostname string, pubSub *events.PubSub, allStarted *sync.WaitGroup) {
	// Register callback to publish engine process exit events.
	engine.OnInstanceExit(publishInstanceExitFn(pubSub.Publish, hostname))

	// Register callback to publish engine format requested events.
	engine.OnAwaitFormat(publishFormatRequiredFn(pubSub.Publish, hostname))

	var onceReady sync.Once
	engine.OnReady(func(_ context.Context) error {
		// Indicate that engine has been started, only do this
		// the first time that the engine starts as shared
		// memory persists between engine restarts.
		onceReady.Do(func() {
			allStarted.Done()
		})

		return nil
	})
}

func configureFirstEngine(ctx context.Context, engine *EngineInstance, sysdb *system.Database, joinFn systemJoinFn) {
	if !sysdb.IsReplica() {
		return
	}

	// Start the system db after instance 0's SCM is ready.
	var onceStorageReady sync.Once
	engine.OnStorageReady(func(_ context.Context) (err error) {
		onceStorageReady.Do(func() {
			// NB: We use the outer context rather than
			// the closure context in order to avoid
			// tying the db to the instance.
			err = errors.Wrap(sysdb.Start(ctx),
				"failed to start system db",
			)
		})

		return
	})

	if !sysdb.IsBootstrap() {
		return
	}

	// For historical reasons, we reserve rank 0 for the first
	// instance on the raft bootstrap server. This implies that
	// rank 0 will always be associated with a MS replica, but
	// it is not guaranteed to always be the leader.
	engine.joinSystem = func(ctx context.Context, req *control.SystemJoinReq) (*control.SystemJoinResp, error) {
		if sb := engine.getSuperblock(); !sb.ValidRank {
			engine.log.Debug("marking bootstrap instance as rank 0")
			req.Rank = 0
			sb.Rank = system.NewRankPtr(0)
		}

		return joinFn(ctx, req)
	}
}

// registerTelemetryCallbacks sets telemetry related callbacks to
// be triggered when all engines have been started.
func registerTelemetryCallbacks(ctx context.Context, srv *server) {
	telemPort := srv.cfg.TelemetryPort
	if telemPort == 0 {
		return
	}

	srv.OnEnginesStarted(func(ctxIn context.Context) error {
		srv.log.Debug("starting Prometheus exporter")
		cleanup, err := startPrometheusExporter(ctxIn, srv.log, telemPort, srv.harness.Instances())
		if err != nil {
			return err
		}
		srv.OnShutdown(cleanup)
		return nil
	})
}

// registerFollowerSubscriptions stops handling received forwarded (in addition
// to local) events and starts forwarding events to the new MS leader.
// Log events on the host that they were raised (and first published) on.
// This is the initial behavior before leadership has been determined.
func registerFollowerSubscriptions(srv *server) {
	srv.pubSub.Reset()
	srv.pubSub.Subscribe(events.RASTypeAny, srv.evtLogger)
	srv.pubSub.Subscribe(events.RASTypeStateChange, srv.evtForwarder)
}

// registerLeaderSubscriptions stops forwarding events to MS and instead starts
// handling received forwardede(and local) events.
func registerLeaderSubscriptions(srv *server) {
	srv.pubSub.Reset()
	srv.pubSub.Subscribe(events.RASTypeAny, srv.evtLogger)
	srv.pubSub.Subscribe(events.RASTypeStateChange, srv.membership)
	srv.pubSub.Subscribe(events.RASTypeStateChange, srv.sysdb)
	srv.pubSub.Subscribe(events.RASTypeStateChange,
		events.HandlerFunc(func(ctx context.Context, evt *events.RASEvent) {
			switch evt.ID {
			case events.RASSwimRankDead:
				ts, err := evt.GetTimestamp()
				if err != nil {
					srv.log.Errorf("bad event timestamp %q: %s", evt.Timestamp, err)
					return
				}
				srv.log.Debugf("%s marked rank %d:%x dead @ %s", evt.Hostname, evt.Rank, evt.Incarnation, ts)
				// Mark the rank as unavailable for membership in
				// new pools, etc. Do group update on success.
				if err := srv.membership.MarkRankDead(system.Rank(evt.Rank), evt.Incarnation); err == nil {
					srv.mgmtSvc.reqGroupUpdate(ctx, false)
				}
			}
		}))
}

func getGrpcOpts(cfgTransport *security.TransportConfig) ([]grpc.ServerOption, error) {
	unaryInterceptors := []grpc.UnaryServerInterceptor{
		unaryErrorInterceptor,
		unaryStatusInterceptor,
	}
	streamInterceptors := []grpc.StreamServerInterceptor{
		streamErrorInterceptor,
	}
	tcOpt, err := security.ServerOptionForTransportConfig(cfgTransport)
	if err != nil {
		return nil, err
	}
	srvOpts := []grpc.ServerOption{tcOpt}

	uintOpt, err := unaryInterceptorForTransportConfig(cfgTransport)
	if err != nil {
		return nil, err
	}
	if uintOpt != nil {
		unaryInterceptors = append(unaryInterceptors, uintOpt)
	}
	sintOpt, err := streamInterceptorForTransportConfig(cfgTransport)
	if err != nil {
		return nil, err
	}
	if sintOpt != nil {
		streamInterceptors = append(streamInterceptors, sintOpt)
	}

	return append(srvOpts, []grpc.ServerOption{
		grpc.ChainUnaryInterceptor(unaryInterceptors...),
		grpc.ChainStreamInterceptor(streamInterceptors...),
	}...), nil
}

type netInterface interface {
	Addrs() ([]net.Addr, error)
}

func getSrxSetting(cfg *config.Server) (int32, error) {
	if len(cfg.Engines) == 0 {
		return -1, nil
	}

	srxVarName := "FI_OFI_RXM_USE_SRX"
	getSetting := func(ev string) (bool, int32, error) {
		kv := strings.Split(ev, "=")
		if len(kv) != 2 {
			return false, -1, nil
		}
		if kv[0] != srxVarName {
			return false, -1, nil
		}
		v, err := strconv.ParseInt(kv[1], 10, 32)
		if err != nil {
			return false, -1, err
		}
		return true, int32(v), nil
	}

	engineVals := make([]int32, len(cfg.Engines))
	for idx, ec := range cfg.Engines {
		engineVals[idx] = -1 // default to unset
		for _, ev := range ec.EnvVars {
			if match, engSrx, err := getSetting(ev); err != nil {
				return -1, err
			} else if match {
				engineVals[idx] = engSrx
				break
			}
		}

		for _, pte := range ec.EnvPassThrough {
			if pte == srxVarName {
				return -1, errors.Errorf("%s may not be set as a pass-through env var", srxVarName)
			}
		}
	}

	cliSrx := engineVals[0]
	for i := 1; i < len(engineVals); i++ {
		if engineVals[i] != cliSrx {
			return -1, errors.Errorf("%s setting must be the same for all engines", srxVarName)
		}
	}

	// If the SRX config was not explicitly set via env vars, use the
	// global config value.
	if cliSrx == -1 {
		cliSrx = int32(common.BoolAsInt(!cfg.Fabric.DisableSRX))
	}

	return cliSrx, nil
}

func checkFabricInterface(name string, lookup func(string) (netInterface, error)) error {
	if name == "" {
		return errors.New("no name provided")
	}

	if lookup == nil {
		return errors.New("no lookup function provided")
	}

	netIF, err := lookup(name)
	if err != nil {
		return err
	}

	addrs, err := netIF.Addrs()
	if err != nil {
		return err
	}

	if len(addrs) == 0 {
		return fmt.Errorf("no network addresses for interface %q", name)
	}

	return nil
}
