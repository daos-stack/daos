//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"bytes"
	"context"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"sync"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"
	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
	"github.com/daos-stack/daos/src/control/system/raft"
)

const (
	// extraHugepages is the number of extra hugepages to request beyond the minimum required,
	// often one or two are not reported as available.
	extraHugepages = 2

	// memCheckThreshold is the percentage of configured RAM-disk size that needs to be met by
	// available memory in order to start the engines.
	memCheckThreshold = 90

	// scanMinHugepageCount is the minimum number of hugepages to allocate in order to satisfy
	// SPDK memory requirements when performing a NVMe device scan.
	scanMinHugepageCount = 128
)

// netListenerFn is a type alias for the net.Listener function signature.
type netListenFn func(string, string) (net.Listener, error)

// ipLookupFn defines the function signature for a helper that can
// be used to resolve a host address to a list of IP addresses.
type ipLookupFn func(string) ([]net.IP, error)

// ifLookupFn defines the function signature for a helper that can be used to resolve a fabric
// interface name to an object of a type that implements the netInterface interface.
type ifLookupFn func(string) (netInterface, error)

// resolveFirstAddr is a helper function to resolve a hostname to a TCP address.
// If the hostname resolves to multiple addresses, the first one is returned.
func resolveFirstAddr(addr string, lookup ipLookupFn) (*net.TCPAddr, error) {
	host, port, err := net.SplitHostPort(addr)
	if err != nil {
		return nil, errors.Wrapf(err, "unable to split %q", addr)
	}
	iPort, err := strconv.Atoi(port)
	if err != nil {
		return nil, errors.Wrapf(err, "unable to convert %q to int", port)
	}
	addrs, err := lookup(host)
	if err != nil {
		return nil, errors.Wrapf(err, "unable to resolve %q", host)
	}

	if len(addrs) == 0 {
		return nil, errors.Errorf("no addresses found for %q", host)
	}

	isIPv4 := func(ip net.IP) bool {
		return ip.To4() != nil
	}
	// Ensure stable ordering of addresses.
	sort.Slice(addrs, func(i, j int) bool {
		if !isIPv4(addrs[i]) && isIPv4(addrs[j]) {
			return false
		} else if isIPv4(addrs[i]) && !isIPv4(addrs[j]) {
			return true
		}
		return bytes.Compare(addrs[i], addrs[j]) < 0
	})

	return &net.TCPAddr{IP: addrs[0], Port: iPort}, nil
}

func getBdevCfgsFromSrvCfg(cfg *config.Server) storage.TierConfigs {
	var bdevCfgs storage.TierConfigs
	for _, engineCfg := range cfg.Engines {
		bdevCfgs = append(bdevCfgs, engineCfg.Storage.Tiers.BdevConfigs()...)
	}

	return bdevCfgs
}

func cfgGetReplicas(cfg *config.Server, lookup ipLookupFn) ([]*net.TCPAddr, error) {
	var dbReplicas []*net.TCPAddr
	for _, ap := range cfg.AccessPoints {
		apAddr, err := resolveFirstAddr(ap, lookup)
		if err != nil {
			return nil, config.FaultConfigBadAccessPoints
		}
		dbReplicas = append(dbReplicas, apAddr)
	}

	return dbReplicas, nil
}

func cfgGetRaftDir(cfg *config.Server) string {
	raftDirName := "control_raft"
	if cfg.Metadata.Path != "" {
		return filepath.Join(cfg.Metadata.Directory(), raftDirName)
	}

	// If no control metadata directory was defined, use the engine SCM storage
	if len(cfg.Engines) == 0 {
		return "" // can't save to SCM
	}
	if len(cfg.Engines[0].Storage.Tiers.ScmConfigs()) == 0 {
		return ""
	}

	return filepath.Join(cfg.Engines[0].Storage.Tiers.ScmConfigs()[0].Scm.MountPoint, raftDirName)
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

type replicaAddrGetter interface {
	ReplicaAddr() (*net.TCPAddr, error)
}

type ctlAddrParams struct {
	port           int
	replicaAddrSrc replicaAddrGetter
	lookupHost     ipLookupFn
}

func getControlAddr(params ctlAddrParams) (*net.TCPAddr, error) {
	ipStr := "0.0.0.0"

	if repAddr, err := params.replicaAddrSrc.ReplicaAddr(); err == nil {
		ipStr = repAddr.IP.String()
	}

	ctlAddr, err := resolveFirstAddr(fmt.Sprintf("[%s]:%d", ipStr, params.port), params.lookupHost)
	if err != nil {
		return nil, errors.Wrap(err, "resolving control address")
	}

	return ctlAddr, nil
}

func createListener(ctlAddr *net.TCPAddr, listen netListenFn) (net.Listener, error) {
	// Create and start listener on management network.
	lis, err := listen("tcp4", fmt.Sprintf("0.0.0.0:%d", ctlAddr.Port))
	if err != nil {
		return nil, errors.Wrap(err, "unable to listen on management interface")
	}

	return lis, nil
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
		fi, err := fis.GetInterfaceOnNetDevice(engine.Fabric.Interface, engine.Fabric.Provider)
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

// Detect the number of engine configs assigned to each NUMA node and return error if engines are
// distributed unevenly across NUMA nodes. Otherwise return sorted list of NUMA nodes in use.
// Configurations where all engines are on a single NUMA node will be allowed.
func getEngineNUMANodes(log logging.Logger, engineCfgs []*engine.Config) ([]string, error) {
	nodeMap := make(map[int]int)
	for _, ec := range engineCfgs {
		nodeMap[int(ec.Storage.NumaNodeIndex)] += 1
	}

	var lastCount int
	nodes := make([]string, 0, len(engineCfgs))
	for k, v := range nodeMap {
		if lastCount != 0 && v != lastCount {
			return nil, FaultEngineNUMAImbalance(nodeMap)
		}
		lastCount = v
		nodes = append(nodes, fmt.Sprintf("%d", k))
	}
	sort.Strings(nodes)

	return nodes, nil
}

// Prepare bdev storage. Assumes validation has already been performed on server config. Hugepages
// are required for both emulated (AIO devices) and real NVMe bdevs. VFIO and IOMMU are not
// required for emulated NVMe.
func prepBdevStorage(srv *server, iommuEnabled bool) error {
	defer srv.logDuration(track("time to prepare bdev storage"))

	if srv.cfg.DisableHugepages {
		srv.log.Debugf("skip nvme prepare as disable_hugepages: true in config")
		return nil
	}

	bdevCfgs := getBdevCfgsFromSrvCfg(srv.cfg)

	// Perform these checks only if non-emulated NVMe is used and user is unprivileged.
	if bdevCfgs.HaveRealNVMe() && srv.runningUser.Username != "root" {
		if srv.cfg.DisableVFIO {
			return FaultVfioDisabled
		}
		if !iommuEnabled {
			return FaultIommuDisabled
		}
	}

	// When requesting to prepare NVMe drives during service start-up, use all addresses
	// specified in engine config BdevList parameters as the PCIAllowList and the server
	// config BdevExclude parameter as the PCIBlockList.

	prepReq := storage.BdevPrepareRequest{
		TargetUser:   srv.runningUser.Username,
		PCIAllowList: strings.Join(bdevCfgs.NVMeBdevs().Devices(), storage.BdevPciAddrSep),
		PCIBlockList: strings.Join(srv.cfg.BdevExclude, storage.BdevPciAddrSep),
		DisableVFIO:  srv.cfg.DisableVFIO,
	}

	enableVMD := true
	if srv.cfg.DisableVMD != nil && *srv.cfg.DisableVMD {
		enableVMD = false
	}

	switch {
	case enableVMD && srv.cfg.DisableVFIO:
		srv.log.Info("VMD not enabled because VFIO disabled in config")
	case enableVMD && !iommuEnabled:
		srv.log.Info("VMD not enabled because IOMMU disabled on platform")
	case enableVMD && bdevCfgs.HaveEmulatedNVMe():
		srv.log.Info("VMD not enabled because emulated NVMe devices found in config")
	default:
		// If no case above matches, set enable VMD flag in request otherwise leave false.
		prepReq.EnableVMD = enableVMD
	}

	if bdevCfgs.HaveBdevs() {
		// The NrHugepages config value is a total for all engines. Distribute allocation
		// of hugepages across each engine's numa node (as validation ensures that
		// TargetsCount is equal for each engine). Assumes an equal number of engine's per
		// numa node.
		numaNodes, err := getEngineNUMANodes(srv.log, srv.cfg.Engines)
		if err != nil {
			return err
		}

		if len(numaNodes) == 0 {
			return errors.New("invalid number of numa nodes detected (0)")
		}

		// Request a few more hugepages than actually required for each NUMA node
		// allocation as some overhead may result in one or two being unavailable.
		prepReq.HugepageCount = srv.cfg.NrHugepages / len(numaNodes)

		// Extra pages to be allocated per engine but take into account the page count
		// will be issued on each NUMA node.
		extraPages := (extraHugepages * len(srv.cfg.Engines)) / len(numaNodes)
		prepReq.HugepageCount += extraPages
		prepReq.HugeNodes = strings.Join(numaNodes, ",")

		srv.log.Debugf("allocating %d hugepages on each of these numa nodes: %v",
			prepReq.HugepageCount, numaNodes)
	} else {
		if srv.cfg.NrHugepages == 0 {
			// If nr_hugepages is unset then set minimum needed for scanning in prepare
			// request.
			prepReq.HugepageCount = scanMinHugepageCount
		} else {
			// If nr_hugepages has been set manually but no bdevs in config then
			// allocate on numa node 0 (for example if a bigger number of hugepages are
			// required in discovery mode for an unusually large number of SSDs).
			prepReq.HugepageCount = srv.cfg.NrHugepages
		}

		srv.log.Debugf("allocating %d hugepages on numa node 0", prepReq.HugepageCount)
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

	if srv.cfg.DisableHugepages {
		srv.log.Debugf("skip nvme scan as hugepages have been disabled in config")
		return &storage.BdevScanResponse{}, nil
	}

	nvmeScanResp, err := srv.ctlSvc.NvmeScan(storage.BdevScanRequest{
		DeviceList: getBdevCfgsFromSrvCfg(srv.cfg).Bdevs(),
	})
	if err != nil {
		err = errors.Wrap(err, "NVMe Scan Failed")
		srv.log.Errorf("%s", err)
		return nil, err
	}

	return nvmeScanResp, nil
}

func setEngineBdevs(engine *EngineInstance, scanResp *storage.BdevScanResponse, lastEngineIdx, lastBdevCount *int) error {
	badInput := ""
	switch {
	case engine == nil:
		badInput = "engine"
	case scanResp == nil:
		badInput = "scanResp"
	case lastEngineIdx == nil:
		badInput = "lastEngineIdx"
	case lastBdevCount == nil:
		badInput = "lastBdevCount"
	}
	if badInput != "" {
		return errors.New("nil input param: " + badInput)
	}

	if err := engine.storage.SetBdevCache(*scanResp); err != nil {
		return errors.Wrap(err, "setting engine storage bdev cache")
	}

	// After engine's bdev cache has been set, the cache will only contain details of bdevs
	// identified in the relevant engine config and device addresses will have been verified
	// against NVMe scan results. As any VMD endpoint addresses will have been replaced with
	// backing device addresses, device counts will reflect the number of physical (as opposed
	// to logical) bdevs and engine bdev counts can be accurately compared.

	eIdx := engine.Index()
	bdevCache := engine.storage.GetBdevCache()
	newNrBdevs := len(bdevCache.Controllers)

	// Update last recorded counters if this is the first update or if the number of bdevs is
	// unchanged. If bdev count differs between engines, return fault.
	switch {
	case *lastEngineIdx < 0:
		if *lastBdevCount >= 0 {
			return errors.New("expecting both lastEngineIdx and lastBdevCount to be unset")
		}
		*lastEngineIdx = int(eIdx)
		*lastBdevCount = newNrBdevs
	case *lastBdevCount < 0:
		return errors.New("expecting both lastEngineIdx and lastBdevCount to be set")
	case newNrBdevs == *lastBdevCount:
		*lastEngineIdx = int(eIdx)
	default:
		return config.FaultConfigBdevCountMismatch(int(eIdx), newNrBdevs, *lastEngineIdx, *lastBdevCount)
	}

	return nil
}

func setDaosHelperEnvs(cfg *config.Server, setenv func(k, v string) error) error {
	if cfg.HelperLogFile != "" {
		if err := setenv(pbin.DaosPrivHelperLogFileEnvVar, cfg.HelperLogFile); err != nil {
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

// Minimum recommended number of hugepages has already been calculated and set in config so verify
// we have enough free hugepage memory to satisfy this requirement before setting mem_size and
// hugepage_size parameters for engine.
func updateHugeMemValues(srv *server, ei *EngineInstance, mi *common.MemInfo) error {
	ei.RLock()
	ec := ei.runner.GetConfig()
	eIdx := ec.Index

	if ec.Storage.Tiers.Bdevs().Len() == 0 {
		srv.log.Debugf("skipping mem check on engine %d, no bdevs", eIdx)
		ei.RUnlock()
		return nil
	}
	ei.RUnlock()

	// Calculate mem_size per I/O engine (in MB) from number of hugepages required per engine.
	nrPagesRequired := srv.cfg.NrHugepages / len(srv.cfg.Engines)
	pageSizeMiB := mi.HugepageSizeKiB / humanize.KiByte // kib to mib
	memSizeReqMiB := nrPagesRequired * pageSizeMiB
	memSizeFreeMiB := mi.HugepagesFree * pageSizeMiB

	// If free hugepage mem is not enough to meet requested number of hugepages, log notice and
	// set mem_size engine parameter to free value. Otherwise set to requested value.
	srv.log.Debugf("Per-engine MemSize:%dMB, HugepageSize:%dMB (meminfo: %+v)", memSizeReqMiB,
		pageSizeMiB, *mi)
	if memSizeFreeMiB < memSizeReqMiB {
		srv.log.Noticef("The amount of hugepage memory allocated for engine %d does not "+
			"meet nr_hugepages requested in config: want %s (%d hugepages), got %s ("+
			"%d hugepages)", ei, humanize.IBytes(uint64(humanize.MiByte*memSizeReqMiB)),
			nrPagesRequired, humanize.IBytes(uint64(humanize.MiByte*memSizeFreeMiB)),
			mi.HugepagesFree)
		ei.setMemSize(memSizeFreeMiB)
	} else {
		ei.setMemSize(memSizeReqMiB)
	}

	// Set hugepage_size (MiB) values based on hugepage info.
	ei.setHugepageSz(pageSizeMiB)

	return nil
}

func cleanEngineHugepages(srv *server) error {
	req := storage.BdevPrepareRequest{
		CleanHugepagesOnly: true,
	}

	msg := "cleanup hugepages via bdev backend"

	resp, err := srv.ctlSvc.NvmePrepare(req)
	if err != nil {
		return errors.Wrap(err, msg)
	}

	srv.log.Debugf("%s: %d removed", msg, resp.NrHugepagesRemoved)

	return nil
}

// Provide some confidence that engines will have enough memory to run without OOM failures by
// ensuring reported available memory (of type RAM) is enough to cover at least 90% of engine
// RAM-disk sizes set in the storage config.
//
// Note that check is to be performed after hugepages have been allocated, as such the available
// memory MemInfo value will show that Hugetlb has already been allocated (and therefore no longer
// available) so don't need to take into account hugepage allowances during calculation.
func checkMemForRamdisk(log logging.Logger, memRamdisks, memAvail uint64) error {
	memRequired := (memRamdisks / 100) * uint64(memCheckThreshold)

	msg := fmt.Sprintf("checking MemAvailable (%s) covers at least %d%% of engine ram-disks "+
		"(%s required to cover %s ram-disk mem)",
		humanize.IBytes(memAvail), memCheckThreshold, humanize.IBytes(memRequired),
		humanize.IBytes(memRamdisks))

	if memAvail < memRequired {
		log.Errorf("%s: available mem too low to support ramdisk size", msg)

		return storage.FaultRamdiskLowMem("Available", memRamdisks, memRequired, memAvail)
	}

	log.Debugf("%s: check successful!", msg)

	return nil
}

func checkEngineTmpfsMem(srv *server, ei *EngineInstance, mi *common.MemInfo) error {
	sc, err := ei.storage.GetScmConfig()
	if err != nil {
		return err
	}

	if sc.Class != storage.ClassRam {
		return nil // no ramdisk to check
	}

	memRamdisk := uint64(sc.Scm.RamdiskSize) * humanize.GiByte
	memAvail := uint64(mi.MemAvailableKiB) * humanize.KiByte

	if err := checkMemForRamdisk(srv.log, memRamdisk, memAvail); err != nil {
		return err
	}

	return nil
}

func registerEngineEventCallbacks(srv *server, engine *EngineInstance, allStarted *sync.WaitGroup) {
	// Register callback to publish engine process exit events.
	engine.OnInstanceExit(createPublishInstanceExitFunc(srv.pubSub.Publish, srv.hostname))

	engine.OnInstanceExit(func(_ context.Context, _ uint32, _ ranklist.Rank, _ error, _ int) error {
		if engine.storage.BdevRoleMetaConfigured() {
			return engine.storage.UnmountTmpfs()
		}
		return nil
	})

	// Register callback to publish engine format requested events.
	engine.OnAwaitFormat(createPublishFormatRequiredFunc(srv.pubSub.Publish, srv.hostname))

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
		srv.log.Debugf("engine %d: storage ready", engine.Index())

		// Attempt to remove unused hugepages, log error only.
		if err := cleanEngineHugepages(srv); err != nil {
			srv.log.Errorf(err.Error())
		}

		// Retrieve up-to-date meminfo to check resource availability.
		mi, err := common.GetMemInfo()
		if err != nil {
			return err
		}

		// Update engine memory related config parameters before starting.
		if err := updateHugeMemValues(srv, engine, mi); err != nil {
			return errors.Wrap(err, "updating engine memory parameters")
		}

		// Check available RAM can satisfy tmpfs size before starting a new engine.
		if err := checkEngineTmpfsMem(srv, engine, mi); err != nil {
			return errors.Wrap(err, "check ram available for engine tmpfs")
		}

		return nil
	})
}

func configureFirstEngine(ctx context.Context, engine *EngineInstance, sysdb *raft.Database, join systemJoinFn) {
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
			sb.Rank = ranklist.NewRankPtr(0)
		}

		return join(ctx, req)
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
				if err := srv.membership.MarkRankDead(ranklist.Rank(evt.Rank), evt.Incarnation); err != nil {
					srv.log.Errorf("failed to mark rank %d:%x dead: %s", evt.Rank, evt.Incarnation, err)
					if system.IsNotLeader(err) {
						// If we've lost leadership while processing the event,
						// attempt to re-forward it to the new leader.
						evt = evt.WithForwarded(false).WithForwardable(true)
						srv.log.Debugf("re-forwarding rank dead event for %d:%x", evt.Rank, evt.Incarnation)
						srv.evtForwarder.OnEvent(ctx, evt)
					}
					return
				}
				srv.mgmtSvc.reqGroupUpdate(ctx, false)
			}
		}))

	// Add a debounce to throttle multiple SWIM Rank Dead events for the same rank/incarnation.
	srv.pubSub.Debounce(events.RASSwimRankDead, 0, func(ev *events.RASEvent) string {
		return strconv.FormatUint(uint64(ev.Rank), 10) + ":" + strconv.FormatUint(ev.Incarnation, 10)
	})
}

// getGrpcOpts generates a set of gRPC options for the server based on the supplied configuration.
func getGrpcOpts(log logging.Logger, cfgTransport *security.TransportConfig, ldrChk func() bool) ([]grpc.ServerOption, error) {
	unaryInterceptors := []grpc.UnaryServerInterceptor{
		unaryLoggingInterceptor(log, ldrChk), // must be first in order to properly log errors
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

func checkFabricInterface(name string, lookup ifLookupFn) error {
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
