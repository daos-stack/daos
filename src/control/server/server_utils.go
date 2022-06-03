//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"net"
	"os"
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

const scanMinHugePageCount = 128

func engineCfgGetBdevs(engineCfg *engine.Config) *storage.BdevDeviceList {
	bdevs := []string{}
	for _, bc := range engineCfg.Storage.Tiers.BdevConfigs() {
		bdevs = append(bdevs, bc.Bdev.DeviceList.Devices()...)
	}

	return storage.MustNewBdevDeviceList(bdevs...)
}

func cfgGetBdevs(cfg *config.Server) *storage.BdevDeviceList {
	bdevs := []string{}
	for _, engineCfg := range cfg.Engines {
		bdevs = append(bdevs, engineCfgGetBdevs(engineCfg).Devices()...)
	}

	return storage.MustNewBdevDeviceList(bdevs...)
}

func cfgHasBdevs(cfg *config.Server) bool {
	return cfgGetBdevs(cfg).Len() != 0
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

func writeCoreDumpFilter(log logging.Logger, path string, filter uint8) error {
	f, err := os.OpenFile(path, os.O_WRONLY, 0644)
	if err != nil {
		// Work around a testing oddity that seems to be related to launching
		// the server via SSH, with the result that the /proc file is unwritable.
		if os.IsPermission(err) {
			log.Debugf("Unable to write core dump filter to %s: %s", path, err)
			return nil
		}
		return errors.Wrapf(err, "unable to open core dump filter file %s", path)
	}
	defer f.Close()

	_, err = f.WriteString(fmt.Sprintf("0x%x\n", filter))
	return err
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
func updateFabricEnvars(log logging.Logger, cfg *engine.Config, fis *hardware.FabricInterfaceSet) error {
	// In the case of some providers, mercury uses the interface name
	// such as ib0, while OFI uses the device name such as hfi1_0 CaRT and
	// Mercury will now support the new OFI_DOMAIN environment variable so
	// that we can specify the correct device for each.
	if !cfg.HasEnvVar("OFI_DOMAIN") {
		fi, err := fis.GetInterfaceOnNetDevice(cfg.Fabric.Interface, cfg.Fabric.Provider)
		if err != nil {
			return errors.Wrapf(err, "unable to determine device domain for %s", cfg.Fabric.Interface)
		}
		log.Debugf("setting OFI_DOMAIN=%s for %s", fi.Name, cfg.Fabric.Interface)
		envVar := "OFI_DOMAIN=" + fi.Name
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

// Detect if any engines share numa nodes and if that's the case, allocate only on the shared numa
// node and notify user.
func getEngineNUMANodes(log logging.Logger, engineCfgs []*engine.Config) []string {
	nodeMap := make(map[string]bool)
	nodes := make([]string, 0, len(engineCfgs))
	for _, ec := range engineCfgs {
		nn := fmt.Sprintf("%d", ec.Storage.NumaNodeIndex)
		if nodeMap[nn] {
			log.Infof("Multiple engines assigned to NUMA node %s, "+
				"allocating all hugepages on this node.", nn)
			nodes = []string{nn}
			break
		}
		nodeMap[nn] = true
		nodes = append(nodes, nn)
	}

	return nodes
}

func prepBdevStorage(srv *server, iommuEnabled bool) error {
	defer srv.logDuration(track("time to prepare bdev storage"))

	hasBdevs := cfgHasBdevs(srv.cfg)

	if hasBdevs {
		// Perform these checks to avoid even trying a prepare if the system isn't
		// configured properly.
		if srv.runningUser.Username != "root" {
			if srv.cfg.DisableVFIO {
				return FaultVfioDisabled
			}

			if !iommuEnabled {
				return FaultIommuDisabled
			}
		}
	} else if srv.cfg.NrHugepages < 0 {
		srv.log.Debugf("skip nvme prepare as no bdevs in cfg and nr_hugepages: -1 in config")
		return nil
	}

	prepReq := storage.BdevPrepareRequest{
		TargetUser:   srv.runningUser.Username,
		PCIAllowList: strings.Join(srv.cfg.BdevInclude, storage.BdevPciAddrSep),
		PCIBlockList: strings.Join(srv.cfg.BdevExclude, storage.BdevPciAddrSep),
		DisableVFIO:  srv.cfg.DisableVFIO,
	}

	switch {
	case !srv.cfg.DisableVMD && srv.cfg.DisableVFIO:
		srv.log.Info("VMD not enabled because VFIO disabled in config")
	case !srv.cfg.DisableVMD && !iommuEnabled:
		srv.log.Info("VMD not enabled because IOMMU disabled on system")
	default:
		prepReq.EnableVMD = !srv.cfg.DisableVMD
	}

	if hasBdevs {
		// The NrHugepages config value is a total for all engines. Distribute allocation
		// of hugepages equally across each engine's numa node (as validation ensures that
		// TargetsCount is equal for each engine).
		numaNodes := getEngineNUMANodes(srv.log, srv.cfg.Engines)

		if len(numaNodes) == 0 {
			return errors.New("invalid number of numa nodes detected (0)")
		}

		// Request a few more hugepages than actually required for each NUMA node
		// allocation as some overhead may result in one or two being unavailable.
		prepReq.HugePageCount = srv.cfg.NrHugepages / len(numaNodes)
		prepReq.HugePageCount += common.ExtraHugePages
		prepReq.HugeNodes = strings.Join(numaNodes, ",")

		srv.log.Debugf("allocating %d hugepages on each of these numa nodes: %v",
			prepReq.HugePageCount, numaNodes)
	} else {
		if srv.cfg.NrHugepages == 0 {
			// If nr_hugepages is unset then set minimum needed for scanning in prepare
			// request.
			prepReq.HugePageCount = scanMinHugePageCount
		} else {
			// If nr_hugepages has been set manually but no bdevs in config then
			// allocate on numa node 0 (for example if a bigger number of hugepages are
			// required in discovery mode for an unusually large number of SSDs).
			prepReq.HugePageCount = srv.cfg.NrHugepages
		}

		srv.log.Debugf("allocating %d hugepages on numa node 0", prepReq.HugePageCount)
	}

	// Run prepare to bind devices to user-space driver and allocate hugepages.
	//
	// TODO: should be passing root context into prepare request to
	//       facilitate cancellation.
	if _, err := srv.ctlSvc.NvmePrepare(prepReq); err != nil {
		srv.log.Errorf("automatic NVMe prepare failed: %s", err)
	}

	return nil
}

// scanBdevStorage performs discovery and validates existence of configured NVMe SSDs.
func scanBdevStorage(srv *server) (*storage.BdevScanResponse, error) {
	defer srv.logDuration(track("time to scan bdev storage"))

	if srv.cfg.NrHugepages < 0 {
		srv.log.Debugf("skip nvme scan as hugepages have been disabled in config")
		return &storage.BdevScanResponse{}, nil
	}

	nvmeScanResp, err := srv.ctlSvc.NvmeScan(storage.BdevScanRequest{
		DeviceList:  cfgGetBdevs(srv.cfg),
		BypassCache: true, // init cache on first scan
	})
	if err != nil {
		err = errors.Wrap(err, "NVMe Scan Failed")
		srv.log.Errorf("%s", err)
		return nil, err
	}

	return nvmeScanResp, nil
}

// Minimum recommended number of hugepages has already been calculated and set in config so verify
// we have enough free hugepage memory to satisfy this requirement before setting mem_size and
// hugepage_size parameters for engine.
func updateMemValues(srv *server, ei *EngineInstance, getHugePageInfo common.GetHugePageInfoFn) error {
	ei.RLock()
	engineCfg := ei.runner.GetConfig()
	engineIdx := engineCfg.Index
	if engineCfgGetBdevs(engineCfg).Len() == 0 {
		srv.log.Debugf("skipping mem check on engine %d, no bdevs", engineIdx)
		ei.RUnlock()
		return nil
	}
	ei.RUnlock()

	// Retrieve up-to-date hugepage info to check that we got the requested number of hugepages.
	hpi, err := getHugePageInfo()
	if err != nil {
		return err
	}

	// Calculate mem_size per I/O engine (in MB) from number of hugepages required per engine.
	nrPagesRequired := srv.cfg.NrHugepages / len(srv.cfg.Engines)
	pageSizeMb := hpi.PageSizeKb >> 10
	memSizeReqMb := nrPagesRequired * pageSizeMb
	memSizeFreeMb := hpi.Free * pageSizeMb

	// Fail if free hugepage mem is not enough to sustain average I/O workload (~1GB).
	if memSizeFreeMb < memSizeReqMb {
		srv.log.Errorf("huge page info: %+v", *hpi)

		return FaultInsufficientFreeHugePageMem(int(engineIdx), memSizeReqMb, memSizeFreeMb,
			nrPagesRequired, hpi.Free)
	}
	srv.log.Debugf("Per-engine MemSize:%dMB, HugepageSize:%dMB", memSizeReqMb, pageSizeMb)

	// Set engine mem_size and hugepage_size (MiB) values based on hugepage info.
	ei.setMemSize(memSizeReqMb)
	ei.setHugePageSz(pageSizeMb)

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

func cleanEngineHugePagesFn(log logging.Logger, username string, svc *ControlService) onInstanceExitFn {
	return func(ctx context.Context, engineIdx uint32, _ system.Rank, _ error, exPid uint64) error {
		msg := fmt.Sprintf("cleaning engine %d (pid %d) hugepages after exit", engineIdx, exPid)

		prepReq := storage.BdevPrepareRequest{
			CleanHugePagesOnly: true,
			CleanHugePagesPID:  exPid,
			TargetUser:         username,
		}

		resp, err := svc.NvmePrepare(prepReq)
		if err != nil {
			err = errors.Wrap(err, msg)
			log.Errorf(err.Error())
			return err
		}

		log.Debugf("%s, %d removed", msg, resp.NrHugePagesRemoved)

		return nil
	}
}

func registerEngineEventCallbacks(srv *server, engine *EngineInstance, allStarted *sync.WaitGroup) {
	// Register callback to publish engine process exit events.
	engine.OnInstanceExit(publishInstanceExitFn(srv.pubSub.Publish, srv.hostname))

	// Register callback to clear hugepages used by engine process after it has exited.
	engine.OnInstanceExit(cleanEngineHugePagesFn(srv.log, srv.runningUser.Username, srv.ctlSvc))

	// Register callback to publish engine format requested events.
	engine.OnAwaitFormat(publishFormatRequiredFn(srv.pubSub.Publish, srv.hostname))

	var onceReady sync.Once
	engine.OnReady(func(_ context.Context) error {
		// Indicate that engine has been started, only do this the first time that the
		// engine starts as shared memory persists between engine restarts.
		onceReady.Do(func() {
			allStarted.Done()
		})

		return nil
	})

	// Register callback to update engine cfg mem_size after format.
	engine.OnStorageReady(func(_ context.Context) error {
		// Retrieve up-to-date hugepage info to evaluate and assign available memory.
		return errors.Wrap(updateMemValues(srv, engine, common.GetHugePageInfo),
			"updating engine memory parameters")
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
// handling received forwarded (and local) events.
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

	// Add a debounce to throttle multiple SWIM Rank Dead events for the same rank/incarnation.
	srv.pubSub.Debounce(events.RASSwimRankDead, 0, func(ev *events.RASEvent) string {
		return strconv.FormatUint(uint64(ev.Rank), 10) + ":" + strconv.FormatUint(ev.Incarnation, 10)
	})
}

func getGrpcOpts(cfgTransport *security.TransportConfig) ([]grpc.ServerOption, error) {
	unaryInterceptors := []grpc.UnaryServerInterceptor{
		unaryErrorInterceptor,
		unaryStatusInterceptor,
		unaryVersionInterceptor,
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
