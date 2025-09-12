//
// (C) Copyright 2021-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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
	"time"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"
	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
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
	// memCheckThreshold is the percentage of configured RAM-disk size that needs to be met by
	// available memory in order to start the engines.
	memCheckThreshold = 90

	// maxLineChars is the maximum number of chars per line in a formatted byte string.
	maxLineChars = 32
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

func cfgGetReplicas(cfg *config.Server, lookup ipLookupFn) ([]*net.TCPAddr, error) {
	var dbReplicas []*net.TCPAddr
	for _, rep := range cfg.MgmtSvcReplicas {
		repAddr, err := resolveFirstAddr(rep, lookup)
		if err != nil {
			return nil, config.FaultConfigBadMgmtSvcReplicas
		}
		dbReplicas = append(dbReplicas, repAddr)
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
	// Mercury will now support the new D_DOMAIN environment variable so
	// that we can specify the correct device for each.
	if !cfg.HasEnvVar("D_DOMAIN") {
		interfaces, err := cfg.Fabric.GetInterfaces()
		if err != nil {
			return err
		}

		providers, err := cfg.Fabric.GetProviders()
		if err != nil {
			return err
		}

		if len(providers) != len(interfaces) {
			return errors.New("number of providers not equal to number of interfaces")
		}

		domains := []string{}

		for i, p := range providers {
			fi, err := fis.GetInterfaceOnNetDevice(interfaces[i], p)
			if err != nil {
				return errors.Wrapf(err, "unable to determine device domain for %s", interfaces[i])
			}
			domains = append(domains, fi.Name)
		}

		domain := strings.Join(domains, engine.MultiProviderSeparator)
		log.Debugf("setting D_DOMAIN=%s for %s", domain, cfg.Fabric.Interface)
		envVar := "D_DOMAIN=" + domain
		cfg.WithEnvVars(envVar)
	}

	return nil
}

func getFabricNetDevClass(cfg *config.Server, fis *hardware.FabricInterfaceSet) ([]hardware.NetDevClass, error) {
	netDevClass := []hardware.NetDevClass{}
	for index, engine := range cfg.Engines {
		cfgIfaces, err := engine.Fabric.GetInterfaces()
		if err != nil {
			return nil, err
		}

		provs, err := engine.Fabric.GetProviders()
		if err != nil {
			return nil, err
		}

		if len(provs) == 1 {
			for i := range cfgIfaces {
				if i == 0 {
					continue
				}
				provs = append(provs, provs[0])
			}
		}

		if len(cfgIfaces) != len(provs) {
			return nil, fmt.Errorf("number of ifaces (%d) and providers (%d) not equal",
				len(cfgIfaces), len(provs))
		}

		for i, cfgIface := range cfgIfaces {
			fi, err := fis.GetInterfaceOnNetDevice(cfgIface, provs[i])
			if err != nil {
				return nil, err
			}

			ndc := fi.DeviceClass
			if index == 0 {
				netDevClass = append(netDevClass, ndc)
				continue
			}
			if ndc != netDevClass[i] {
				return nil, config.FaultConfigInvalidNetDevClass(index, netDevClass[i],
					ndc, engine.Fabric.Interface)
			}
		}
	}
	return netDevClass, nil
}

// getHugeNodesStr builds HUGENODE string to be used to allocate hugepages through SPDK setup
// script. For each NUMA node request max(existing, configured) hugepages.
func getHugeNodesStr(log logging.Logger, perNumaNrWant int, smi *common.SysMemInfo, numaNodes ...int) (string, error) {
	if len(numaNodes) == 0 {
		return "", errors.New("no numa-nodes supplied")
	}

	nodeNrs := make(map[int]int) // How many nr_hugepages to be set for each NUMA-node.

	for _, nID := range numaNodes {
		if nID < 0 {
			return "", errors.New("invalid negative numa-node supplied")
		}

		found := false
		for _, nn := range smi.NumaNodes {
			if nn.NumaNodeIndex != nID {
				continue
			}
			// Ensure that if there is already sufficient number allocated then the
			// script will request the existing number which results in a no-op.
			//
			// FIXME DAOS-16921: SPDK https://review.spdk.io/c/spdk/spdk/+/25831 adds
			//                   SKIP_HUGE which can be used to simplify this logic.
			if nn.HugepagesTotal >= perNumaNrWant {
				nodeNrs[nID] = nn.HugepagesTotal // Maintain
			} else {
				msg := fmt.Sprintf("NUMA-%d from %d to %d", nID, nn.HugepagesTotal,
					perNumaNrWant)
				log.Noticef("Increasing number of hugepages on %s", msg)
				nodeNrs[nID] = perNumaNrWant // Grow
			}
			found = true
			break
		}

		// Handle case where per-NUMA meminfo is missing. If not available, fall-back to
		// legacy behavior where requested number of pages are set for each NUMA.
		if !found {
			nodeNrs[nID] = perNumaNrWant
		}
	}

	nodeNrsKeys := []int{}
	for k := range nodeNrs {
		nodeNrsKeys = append(nodeNrsKeys, k)
	}
	sort.Ints(nodeNrsKeys)

	// Build string for req.HugeNodes e.g. "HUGENODE='nodes_hp[0]=2048,nodes_hp[1]=512'"
	hnStrs := []string{}
	for _, nID := range nodeNrsKeys {
		hnStrs = append(hnStrs, fmt.Sprintf("nodes_hp[%d]=%d", nID, nodeNrs[nID]))
	}

	return fmt.Sprintf("%s", strings.Join(hnStrs, ",")), nil
}

// SetHugeNodes derives HUGENODE string to be used to allocate hugepages across NUMA nodes in spdk
// setup and sets value in prepare request HugeNodes field. If config is present, use its parameters
// otherwise use HugepageCount from the request and allocate only on NUMA node 0.
func SetHugeNodes(log logging.Logger, srvCfg *config.Server, smi *common.SysMemInfo, req *storage.BdevPrepareRequest) (err error) {
	if req == nil {
		return errors.Errorf("nil %T", req)
	}
	if smi == nil {
		return errors.Errorf("nil %T", smi)
	}

	nrHugepages := req.HugepageCount
	nodes := []int{0}
	if srvCfg != nil {
		nrHugepages = srvCfg.NrHugepages
		nodes, err = srvCfg.GetNumaNodes()
		if err != nil {
			return errors.Wrap(err, "get engine numa nodes from server config")
		}
	}

	perNumaNrWant := nrHugepages / len(nodes)

	log.Debugf("attempting to allocate %d hugepages on nodes %v", perNumaNrWant, nodes)

	hnStr, err := getHugeNodesStr(log, perNumaNrWant, smi, nodes...)
	if err != nil {
		return errors.Wrap(err, "get hugenode string for spdk setup")
	}
	req.HugeNodes = hnStr
	req.HugepageCount = 0 // HugeNodes will be used instead to specify per-NUMA allocations.

	log.Debugf("sending HUGENODE=%q to SPDK setup script", req.HugeNodes)

	return nil
}

// Prepare bdev storage. Assumes validation has already been performed on server config. Hugepages
// are required for both emulated (AIO devices) and real NVMe bdevs. VFIO and IOMMU are not
// mandatory requirements for emulated NVMe.
func prepBdevStorage(srv *server, iommuEnabled bool, smi *common.SysMemInfo) error {
	defer srv.logDuration(track("time to prepare bdev storage"))

	if srv.cfg == nil {
		return errors.New("nil server config")
	}
	if srv.cfg.DisableHugepages {
		srv.log.Debugf("skip nvme prepare as disable_hugepages is set true in config")
		return nil
	}

	bdevCfgs := srv.cfg.GetBdevConfigs()

	// Perform these checks only if non-emulated NVMe is used and user is unprivileged.
	if bdevCfgs.HaveRealNVMe() && srv.runningUser.Username != "root" {
		if srv.cfg.DisableVFIO {
			return FaultVfioDisabled
		}
		if !iommuEnabled {
			return FaultIommuDisabled
		}
	}

	// Clean leftover SPDK hugepages and lockfiles for configured NVMe SSDs before prepare.
	pciAddrs := bdevCfgs.NVMeBdevs().Devices()
	if err := cleanSpdkResources(srv, pciAddrs); err != nil {
		srv.log.Error(errors.Wrap(err, "prepBdevStorage").Error())
	}

	// When requesting to prepare NVMe drives during service start-up, use all addresses
	// specified in engine config BdevList parameters as the PCIAllowList and the server
	// config BdevExclude parameter as the PCIBlockList.

	prepReq := storage.BdevPrepareRequest{
		TargetUser:   srv.runningUser.Username,
		PCIAllowList: strings.Join(pciAddrs, storage.BdevPciAddrSep),
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

	// Set hugepage allocations in prepare request.
	if err := SetHugeNodes(srv.log, srv.cfg, smi, &prepReq); err != nil {
		return errors.Wrap(err, "setting hugenodes in bdev prep request")
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

// Per-NUMA hugepage allocations have been calculated and requested from kernel in prepBdevStorage
// so now set mem-size values for engines and verify there are enough free hugepages to satisfy
// typical DMA buffer requirements.
func setEngineMemSize(srv *server, ei *EngineInstance, smi *common.SysMemInfo) {
	ei.RLock()
	ec := ei.runner.GetConfig()
	eIdx := ec.Index

	if ec.Storage.Tiers.Bdevs().Len() == 0 {
		srv.log.Debugf("skipping mem check on engine %d, no bdevs", eIdx)
		ei.RUnlock()
		return
	}
	ei.RUnlock()

	// Mem-size for each engine to be calculated based on server config total hugepage
	// requirements. Mem-size should be the same for each engine to avoid performance imbalance
	// and will act as memory cap to stop DMA buffer growing beyond mem-size.
	nrPagesRequired := srv.cfg.NrHugepages / len(srv.cfg.Engines)

	// Global (rather than per-NUMA) meminfo stats used to verify sufficient free hugepages as
	// engines should be started even if hugemem has to be used across NUMA boundaries.
	nrPagesFree := smi.HugepagesFree

	// Calculate mem_size per I/O engine (in MB) based on number of pages required per engine.
	pageSizeMiB := smi.HugepageSizeKiB / humanize.KiByte // kib to mib
	memSizeReqMiB := nrPagesRequired * pageSizeMiB
	memSizeFreeMiB := nrPagesFree * pageSizeMiB

	// If free hugepage mem is not enough to meet requested number of hugepages, log notice and
	// set mem_size engine parameter to free value. Otherwise set to requested value.
	memSizeMiB := memSizeReqMiB
	if memSizeFreeMiB < memSizeReqMiB {
		srv.log.Noticef("The amount of hugepage memory available for engine %d (%s, %d "+
			"hugepages) does not meet what is required (%s, %d hugepages)", ei.Index(),
			humanize.IBytes(uint64(humanize.MiByte*memSizeFreeMiB)), nrPagesFree,
			humanize.IBytes(uint64(humanize.MiByte*memSizeReqMiB)), nrPagesRequired)
		memSizeMiB = memSizeFreeMiB
	}

	// Set hugepage_size (MiB) values based on hugepage info.
	srv.log.Debugf("Per-engine MemSize:%dMB, HugepageSize:%dMB (meminfo: %s)", memSizeMiB,
		pageSizeMiB, smi.Summary())
	ei.setMemSize(memSizeMiB)
	ei.setHugepageSz(pageSizeMiB)
}

// Clean SPDK resources, both lockfiles and orphaned hugepages. Orphaned hugepages will be cleaned
// whether or not device PCI addresses are supplied.
func cleanSpdkResources(srv *server, pciAddrs []string) error {
	// For the moment assume that both lockfile and hugepage cleanup should be skipped if
	// hugepages have been disabled in the server config.
	if srv.cfg.DisableHugepages {
		return nil
	}

	req := storage.BdevPrepareRequest{
		CleanSpdkHugepages: true,
		CleanSpdkLockfiles: true,
		PCIAllowList:       strings.Join(pciAddrs, storage.BdevPciAddrSep),
	}

	msg := "cleanup spdk resources"

	resp, err := srv.ctlSvc.NvmePrepare(req)
	if err != nil {
		return errors.Wrap(err, msg)
	}

	srv.log.Debugf("%s: %d hugepages and lockfiles %v removed", msg,
		resp.NrHugepagesRemoved, resp.LockfilesRemoved)

	return nil
}

// Provide some confidence that engines will have enough memory to run without OOM failures by
// ensuring reported available memory (of type RAM) is enough to cover at least 90% of engine
// RAM-disk sizes set in the storage config.
//
// Note that check is to be performed after hugepages have been allocated, as such the available
// memory SysMemInfo value will show that Hugetlb has already been allocated (and therefore no longer
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

func checkEngineTmpfsMem(srv *server, ei *EngineInstance, smi *common.SysMemInfo) error {
	sc, err := ei.storage.GetScmConfig()
	if err != nil {
		return err
	}

	if sc.Class != storage.ClassRam {
		return nil // no ramdisk to check
	}

	memRamdisk := uint64(sc.Scm.RamdiskSize) * humanize.GiByte
	memAvail := uint64(smi.MemAvailableKiB) * humanize.KiByte

	// In the event that tmpfs was already mounted, we need to verify that it
	// is the correct size and that the memory usage still makes sense.
	if isMounted, err := ei.storage.ScmIsMounted(); err == nil && isMounted {
		usage, err := ei.storage.GetScmUsage()
		if err != nil {
			return errors.Wrap(err, "unable to check tmpfs usage")
		}
		// Ensure that the existing ramdisk is not larger than the calculated
		// optimal size, in order to avoid potential OOM situations.
		if usage.TotalBytes > memRamdisk {
			return storage.FaultRamdiskBadSize(usage.TotalBytes, memRamdisk)
		}
		// Looks OK, so we can return early and bypass additional checks.
		srv.log.Debugf("using existing tmpfs of size %s", humanize.IBytes(usage.TotalBytes))
		return nil
	} else if err != nil {
		return errors.Wrap(err, "unable to check for mounted tmpfs")
	}

	if err := checkMemForRamdisk(srv.log, memRamdisk, memAvail); err != nil {
		return err
	}

	return nil
}

// createPublishFormatRequiredFunc returns onAwaitFormatFn which will publish an
// event using the provided publish function to indicate that host is awaiting
// storage format.
func createPublishFormatRequiredFunc(publish func(*events.RASEvent), hostname string) onAwaitFormatFn {
	return func(_ context.Context, engineIdx uint32, formatType string) error {
		evt := events.NewEngineFormatRequiredEvent(hostname, engineIdx, formatType).
			WithRank(uint32(ranklist.NilRank))
		publish(evt)

		return nil
	}
}

func registerEngineEventCallbacks(srv *server, engine *EngineInstance, allStarted *sync.WaitGroup) {
	// Register callback to publish engine process exit events.
	engine.OnInstanceExit(createPublishInstanceExitFunc(srv.pubSub.Publish, srv.hostname))

	engine.OnInstanceExit(func(_ context.Context, _ uint32, _ ranklist.Rank, _ uint64, _ error, _ int) error {
		storageCfg := engine.runner.GetConfig().Storage
		pciAddrs := storageCfg.Tiers.NVMeBdevs().Devices()

		if err := cleanSpdkResources(srv, pciAddrs); err != nil {
			srv.log.Error(
				errors.Wrapf(err, "engine instance %d", engine.Index()).Error())
		}

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

		storageCfg := engine.runner.GetConfig().Storage
		pciAddrs := storageCfg.Tiers.NVMeBdevs().Devices()

		if err := cleanSpdkResources(srv, pciAddrs); err != nil {
			srv.log.Error(
				errors.Wrapf(err, "engine instance %d", engine.Index()).Error())
		}

		// Retrieve up-to-date meminfo to check resource availability.
		smi, err := common.GetSysMemInfo()
		if err != nil {
			return err
		}

		// Update engine memory related config parameters before starting.
		setEngineMemSize(srv, engine, smi)

		// Check available RAM can satisfy tmpfs size before starting a new engine.
		if err := checkEngineTmpfsMem(srv, engine, smi); err != nil {
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
				if ok := checkSysPropSelfHeal(*srv.mgmtSvc, "exclude"); !ok {
					return
				}
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

// checkSysPropSelfHeal sees if system property "self_heal" has flag.
func checkSysPropSelfHeal(svc mgmtSvc, flag string) bool {
	if svc.lastBecameLeader.IsZero() || time.Since(svc.lastBecameLeader) < time.Minute {
		return false
	}

	selfHeal, err := system.GetUserProperty(svc.sysdb, svc.systemProps, daos.SystemPropertySelfHeal.String())
	if system.IsErrSystemAttrNotFound(err) {
		// Assume all flags are set.
		return true
	} else if err != nil {
		svc.log.Errorf("unable to get system property 'self_heal': %s", err)
		return false
	}

	return daos.SystemPropertySelfHealHasFlag(selfHeal, flag)
}

// getGrpcOpts generates a set of gRPC options for the server based on the supplied configuration.
func getGrpcOpts(log logging.Logger, cfgTransport *security.TransportConfig, ldrChk func() bool) ([]grpc.ServerOption, error) {
	unaryInterceptors := []grpc.UnaryServerInterceptor{
		unaryLoggingInterceptor(log, ldrChk), // must be first in order to properly log errors
		unaryErrorInterceptor,
		unaryStatusInterceptor,
		unaryVersionInterceptor(log),
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

// Convert bytestring to format accepted by lspci, 16 bytes per line.
func formatBytestring(in string, sb *strings.Builder) {
	if sb == nil {
		return
	}
	for i, s := range in {
		remainder := i % maxLineChars
		if remainder == 0 {
			sb.WriteString(fmt.Sprintf("%02x: ", i/2))
		}
		sb.WriteString(string(s))
		if i == (len(in)-1) || remainder == maxLineChars-1 {
			// Print newline after last char on line.
			sb.WriteString("\n")
		} else if (i % 2) != 0 {
			// Print space after each double char group.
			sb.WriteString(" ")
		}
	}
}
