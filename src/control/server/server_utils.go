//
// (C) Copyright 2021 Intel Corporation.
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
	"github.com/daos-stack/daos/src/control/lib/netdetect"
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
			if len(bc.Bdev.DeviceList) > 0 {
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
func updateFabricEnvars(ctx context.Context, cfg *engine.Config) error {
	// In the case of ofi+verbs provider, mercury uses the interface name
	// such as ib0, while OFI uses the device name such as hfi1_0 CaRT and
	// Mercury will now support the new OFI_DOMAIN environment variable so
	// that we can specify the correct device for each.
	if strings.HasPrefix(cfg.Fabric.Provider, "ofi+verbs") && !cfg.HasEnvVar("OFI_DOMAIN") {
		deviceAlias, err := netdetect.GetDeviceAlias(ctx, cfg.Fabric.Interface)
		if err != nil {
			return errors.Wrapf(err, "failed to resolve alias for %s", cfg.Fabric.Interface)
		}
		envVar := "OFI_DOMAIN=" + deviceAlias
		cfg.WithEnvVars(envVar)
	}

	return nil
}

// netInit performs all network detection tasks in one place starting with
// netdetect library init and cleaning up on exit. Warn if configured number
// of engines is less than NUMA node count and update-in-place engine configs.
func netInit(ctx context.Context, log *logging.LeveledLogger, cfg *config.Server) (uint32, error) {
	engineCount := len(cfg.Engines)
	if engineCount == 0 {
		log.Debug("no engines configured, skipping network init")
		return 0, nil
	}

	ctx, err := netdetect.Init(ctx)
	if err != nil {
		return 0, err
	}
	defer netdetect.CleanUp(ctx)

	// On a NUMA-aware system, emit a message when the configuration may be
	// sub-optimal.
	numaCount := netdetect.NumNumaNodes(ctx)
	if numaCount > 0 && engineCount > numaCount {
		log.Infof("NOTICE: Detected %d NUMA node(s); %d-server config may not perform as expected",
			numaCount, engineCount)
	}

	netDevClass, err := cfg.CheckFabric(ctx)
	if err != nil {
		return 0, errors.Wrap(err, "validate fabric config")
	}

	for _, engine := range cfg.Engines {
		if err := updateFabricEnvars(ctx, engine); err != nil {
			return 0, errors.Wrap(err, "update engine fabric envars")
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
		PCIAllowlist:  strings.Join(srv.cfg.BdevInclude, " "),
		PCIBlocklist:  strings.Join(srv.cfg.BdevExclude, " "),
		DisableVFIO:   srv.cfg.DisableVFIO,
		DisableVMD:    srv.cfg.DisableVMD || srv.cfg.DisableVFIO || !iommuEnabled,
		// TODO: pass vmd include list
	}

	hasBdevs := cfgHasBdevs(srv.cfg)
	if hasBdevs {
		// The config value is intended to be per-engine, so we need to adjust
		// based on the number of engines.
		prepReq.HugePageCount = srv.cfg.NrHugepages * len(srv.cfg.Engines)

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

	// TODO: should be passing root context into prepare request to
	//       facilitate cancellation.
	srv.log.Debugf("automatic NVMe prepare req: %+v", prepReq)
	if _, err := srv.ctlSvc.NvmePrepare(prepReq); err != nil {
		srv.log.Errorf("automatic NVMe prepare failed (check configuration?)\n%s", err)
	}

	hugePages, err := hpiGetter()
	if err != nil {
		return errors.Wrap(err, "unable to read system hugepage info")
	}

	if hasBdevs {
		// Double-check that we got the requested number of huge pages after prepare.
		if hugePages.Free < prepReq.HugePageCount {
			return FaultInsufficientFreeHugePages(hugePages.Free, prepReq.HugePageCount)
		}
	}

	for _, engineCfg := range srv.cfg.Engines {
		// Calculate mem_size per I/O engine (in MB)
		PageSizeMb := hugePages.PageSizeKb >> 10
		engineCfg.MemSize = hugePages.Free / len(srv.cfg.Engines)
		engineCfg.MemSize *= PageSizeMb
		// Pass hugepage size, do not assume 2MB is used
		engineCfg.HugePageSz = PageSizeMb
		srv.log.Debugf("MemSize:%dMB, HugepageSize:%dMB", engineCfg.MemSize, engineCfg.HugePageSz)
		// Warn if hugepages are not enough to sustain average
		// I/O workload (~1GB), ignore warning if only using SCM backend
		if !hasBdevs {
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
				// Mark the rank as unavailable for membership in
				// new pools, etc. Do group update on success.
				if err := srv.membership.MarkRankDead(system.Rank(evt.Rank)); err == nil {
					srv.mgmtSvc.reqGroupUpdate(ctx)
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
	getSetting := func(ev string) (bool, int32) {
		kv := strings.Split(ev, "=")
		if len(kv) != 2 {
			return false, -1
		}
		if kv[0] != srxVarName {
			return false, -1
		}
		v, err := strconv.ParseInt(kv[1], 10, 32)
		if err != nil {
			return true, -1
		}
		return true, int32(v)
	}

	engineVals := make([]int32, len(cfg.Engines))
	for idx, ec := range cfg.Engines {
		engineVals[idx] = -1 // default to unset
		for _, ev := range ec.EnvVars {
			if match, engSrx := getSetting(ev); match {
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
