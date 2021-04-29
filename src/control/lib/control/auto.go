//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"fmt"
	"math"
	"sort"

	"github.com/pkg/errors"

	nd "github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	scmMountPrefix        = "/mnt/daos"
	scmBdevDir            = "/dev"
	defaultFiPort         = 31416
	defaultFiPortInterval = 1000
	defaultTargetCount    = 16
	defaultEngineLogFile  = "/tmp/daos_engine"
	defaultControlLogFile = "/tmp/daos_server.log"
	// NetDevAny matches any netdetect network device class
	NetDevAny = math.MaxUint32

	errNoNuma            = "zero numa nodes reported on hosts %s"
	errUnsupNetDevClass  = "unsupported net dev class in request: %s"
	errInsufNrIfaces     = "insufficient matching %s network interfaces, want %d got %d %v"
	errInsufNrPMemGroups = "insufficient number of pmem device numa groups %v, want %d got %d"
	errInvalNrEngines    = "unexpected number of engines requested, want %d got %d"
	errInsufNrSSDs       = "insufficient number of ssds for numa %d, want %d got %d"
	errInvalNrCores      = "invalid number of cores for numa %d"
)

type (
	// ConfigGenerateReq contains the inputs for the request.
	ConfigGenerateReq struct {
		unaryRequest
		msRequest
		NrEngines    int
		MinNrSSDs    int
		NetClass     uint32
		Client       UnaryInvoker
		HostList     []string
		AccessPoints []string
		Log          logging.Logger
	}

	// ConfigGenerateResp contains the request response.
	ConfigGenerateResp struct {
		HostErrorsResp
		ConfigOut *config.Server
	}
)

// ConfigGenerate attempts to automatically detect hardware and generate a DAOS
// server config file for a set of hosts with homogeneous hardware setup.
//
// Returns API response and error.
func ConfigGenerate(ctx context.Context, req ConfigGenerateReq) (*ConfigGenerateResp, error) {
	req.Log.Debugf("ConfigGenerate called with request %+v", req)

	if len(req.HostList) == 0 {
		return nil, errors.New("no hosts specified")
	}

	if len(req.AccessPoints) == 0 {
		return nil, errors.New("no access points specified")
	}

	nd, hostErrs, err := getNetworkDetails(ctx, req)
	if err != nil {
		return checkHostErrors(hostErrs), err
	}

	sd, hostErrs, err := getStorageDetails(ctx, req, nd.engineCount)
	if err != nil {
		return checkHostErrors(hostErrs), err
	}

	ccs, err := getCPUDetails(req.Log, sd.numaSSDs, nd.numaCoreCount)
	if err != nil {
		return nil, err
	}

	cfg, err := genConfig(req.Log, req.AccessPoints, nd, sd, ccs)
	if err != nil {
		return nil, err
	}

	return &ConfigGenerateResp{ConfigOut: cfg}, nil
}

func checkHostErrors(hes *HostErrorsResp) *ConfigGenerateResp {
	if hes == nil {
		hes = &HostErrorsResp{}
	}

	return &ConfigGenerateResp{HostErrorsResp: *hes}
}

// getNetworkSet retrieves the result of network scan over host list and
// verifies that there is only a single network set in response which indicates
// that network hardware setup is homogeneous across all hosts.
//
// Return host errors, network scan results for the host set or error.
func getNetworkSet(ctx context.Context, log logging.Logger, hostList []string, client UnaryInvoker) (*HostFabricSet, *HostErrorsResp, error) {
	scanReq := new(NetworkScanReq)
	scanReq.SetHostList(hostList)

	scanResp, err := NetworkScan(ctx, client, scanReq)
	if err != nil {
		return nil, nil, err
	}

	if len(scanResp.GetHostErrors()) > 0 {
		return nil, &scanResp.HostErrorsResp, scanResp.Errors()
	}

	// verify homogeneous network
	switch len(scanResp.HostFabrics) {
	case 0:
		return nil, nil, errors.New("no host responses")
	case 1: // success
	default: // more than one means non-homogeneous hardware
		log.Info("Heterogeneous network hardware configurations detected, " +
			"cannot proceed. The following sets of hosts have different " +
			"network hardware:")
		for _, hns := range scanResp.HostFabrics {
			log.Info(hns.HostSet.String())
		}

		return nil, nil, errors.New("network hardware not consistent across hosts")
	}

	networkSet := scanResp.HostFabrics[scanResp.HostFabrics.Keys()[0]]

	log.Debugf("Network hardware is consistent for hosts %s:\n\t%v",
		networkSet.HostSet, networkSet.HostFabric.Interfaces)

	return networkSet, nil, nil
}

// numaNetIfaceMap is an alias for a map of NUMA node ID to optimal
// fabric network interface.
type numaNetIfaceMap map[int]*HostFabricInterface

// hasNUMAs returns true if interfaces exist for given NUMA node range.
func (nnim numaNetIfaceMap) hasNUMAs(numaCount int) bool {
	for nn := 0; nn < numaCount; nn++ {
		if _, exists := nnim[nn]; !exists {
			return false
		}
	}

	return true
}

// classInterfaces is an alias for a map of netdev class ID to slice of
// fabric network interfaces.
type classInterfaces map[uint32]numaNetIfaceMap

// add network device to bucket corresponding to provider, network class type and
// NUMA node binding. Ignore add if there is an existing entry as the interfaces
// are processed in descending order of performance (best first).
func (cis classInterfaces) add(log logging.Logger, iface *HostFabricInterface) {
	nn := int(iface.NumaNode)
	if _, exists := cis[iface.NetDevClass]; !exists {
		cis[iface.NetDevClass] = make(numaNetIfaceMap)
	}
	if _, exists := cis[iface.NetDevClass][nn]; exists {
		return // already have interface for this NUMA
	}
	log.Debugf("%s class iface %s found for NUMA %d", nd.DevClassName(iface.NetDevClass),
		iface.Device, nn)
	cis[iface.NetDevClass][nn] = iface
}

// parseInterfaces processes network devices in scan result, adding to a match
// list given the following conditions:
// IF class == (ether OR infiniband) AND requested_class == (ANY OR <class>).
//
// Returns when network devices matching criteria have been found for each
// required NUMA node.
func parseInterfaces(log logging.Logger, reqClass uint32, engineCount int, interfaces []*HostFabricInterface) (numaNetIfaceMap, bool) {
	// sort network interfaces by priority to get best available
	sort.Slice(interfaces, func(i, j int) bool {
		return interfaces[i].Priority < interfaces[j].Priority
	})

	var matches numaNetIfaceMap
	buckets := make(map[string]classInterfaces)
	for _, iface := range interfaces {
		switch iface.NetDevClass {
		case nd.Ether, nd.Infiniband:
			switch reqClass {
			case NetDevAny, iface.NetDevClass:
			default:
				continue // iface class not requested
			}
		default:
			continue // iface class unsupported
		}

		// init network device slice for a new provider
		if _, exists := buckets[iface.Provider]; !exists {
			buckets[iface.Provider] = make(classInterfaces)
		}

		buckets[iface.Provider].add(log, iface)
		matches = buckets[iface.Provider][iface.NetDevClass]

		if matches.hasNUMAs(engineCount) {
			return matches, true
		}
	}

	return matches, false
}

// getNetIfaces scans fabric network devices and returns a NUMA keyed map for a
// provider/class combination.
func getNetIfaces(log logging.Logger, reqClass uint32, engineCount int, hfs *HostFabricSet) (numaNetIfaceMap, error) {
	switch reqClass {
	case NetDevAny, nd.Ether, nd.Infiniband:
	default:
		return nil, errors.Errorf(errUnsupNetDevClass, nd.DevClassName(reqClass))
	}

	matchIfaces, complete := parseInterfaces(log, reqClass, engineCount, hfs.HostFabric.Interfaces)
	if !complete {
		class := "best-available"
		if reqClass != NetDevAny {
			class = nd.DevClassName(reqClass)
		}
		return nil, errors.Errorf(errInsufNrIfaces, class, engineCount, len(matchIfaces),
			matchIfaces)
	}

	log.Debugf("selected network interfaces: %v", matchIfaces)

	return matchIfaces, nil
}

type networkDetails struct {
	engineCount   int
	numaIfaces    numaNetIfaceMap
	numaCoreCount int
}

// getNetworkDetails retrieves recommended network interfaces.
//
// Returns map of NUMA node ID to chosen fabric interfaces, number of engines to
// provide mappings for, per-NUMA core count and any host errors.
func getNetworkDetails(ctx context.Context, req ConfigGenerateReq) (*networkDetails, *HostErrorsResp, error) {
	netSet, hostErrs, err := getNetworkSet(ctx, req.Log, req.HostList, req.Client)
	if err != nil {
		return nil, hostErrs, err
	}

	nd := &networkDetails{
		engineCount:   req.NrEngines,
		numaCoreCount: int(netSet.HostFabric.CoresPerNuma),
	}
	// set number of engines if unset based on number of NUMA nodes on hosts
	if nd.engineCount == 0 {
		nd.engineCount = int(netSet.HostFabric.NumaCount)
	}
	if nd.engineCount == 0 {
		return nil, nil, errors.Errorf(errNoNuma, netSet.HostSet)
	}

	req.Log.Debugf("engine count for generated config set to %d", nd.engineCount)

	numaIfaces, err := getNetIfaces(req.Log, req.NetClass, nd.engineCount, netSet)
	if err != nil {
		return nil, nil, err
	}
	nd.numaIfaces = numaIfaces

	return nd, nil, nil
}

// getStorageSet retrieves the result of storage scan over host list and
// verifies that there is only a single storage set in response which indicates
// that storage hardware setup is homogeneous across all hosts.
//
// Filter NVMe storage scan so only NUMA affinity and PCI address is taking into
// account by supplying NvmeBasic flag in scan request. This enables
// configuration to work with different combinations of SSD models.
//
// Return host errors, storage scan results for the host set or error.
func getStorageSet(ctx context.Context, log logging.Logger, hostList []string, client UnaryInvoker) (*HostErrorsResp, *HostStorageSet, error) {
	scanReq := &StorageScanReq{NvmeBasic: true}
	scanReq.SetHostList(hostList)

	scanResp, err := StorageScan(ctx, client, scanReq)
	if err != nil {
		return nil, nil, err
	}
	if len(scanResp.GetHostErrors()) > 0 {
		return &scanResp.HostErrorsResp, nil, scanResp.Errors()
	}

	// verify homogeneous storage
	switch len(scanResp.HostStorage) {
	case 0:
		return nil, nil, errors.New("no host responses")
	case 1: // success
	default: // more than one means non-homogeneous hardware
		log.Info("Heterogeneous storage hardware configurations detected, " +
			"cannot proceed. The following sets of hosts have different " +
			"storage hardware:")
		for _, hss := range scanResp.HostStorage {
			log.Info(hss.HostSet.String())
		}

		return nil, nil, errors.New("storage hardware not consistent across hosts")
	}

	storageSet := scanResp.HostStorage[scanResp.HostStorage.Keys()[0]]

	log.Debugf("Storage hardware is consistent for hosts %s:\n\t%s\n\t%s",
		storageSet.HostSet.String(), storageSet.HostStorage.ScmNamespaces.Summary(),
		storageSet.HostStorage.NvmeDevices.Summary())

	return nil, storageSet, nil
}

// numaPMemsMap is an alias for a map of NUMA node ID to slice of string sorted
// PMem block device paths.
type numaPMemsMap map[int]sort.StringSlice

// mapPMems maps NUMA node ID to pmem block device paths, sort paths to attempt
// selection of desired devices if named appropriately in the case that multiple
// devices exist for a given NUMA node.
func mapPMems(nss storage.ScmNamespaces) numaPMemsMap {
	npms := make(numaPMemsMap)
	for _, ns := range nss {
		nn := int(ns.NumaNode)
		npms[nn] = append(npms[nn], fmt.Sprintf("%s/%s", scmBdevDir, ns.BlockDevice))
	}
	for _, pms := range npms {
		pms.Sort()
	}

	return npms
}

// numSSDsMap is an alias for a map of NUMA node ID to slice of NVMe SSD PCI
// addresses.
type numaSSDsMap map[int]sort.StringSlice

// mapSSDs maps NUMA node ID to NVMe SSD PCI addresses, sort addresses.
func mapSSDs(ssds storage.NvmeControllers) numaSSDsMap {
	nssds := make(numaSSDsMap)
	for _, ssd := range ssds {
		nn := int(ssd.SocketID)
		nssds[nn] = append(nssds[nn], ssd.PciAddr)
	}
	for _, ssds := range nssds {
		ssds.Sort()
	}

	return nssds
}

type storageDetails struct {
	numaPMems numaPMemsMap
	numaSSDs  numaSSDsMap
}

// validate checks sufficient PMem devices and SSD NUMA groups exist for the
// required number of engines. Minimum thresholds for SSD group size is also
// checked.
func (sd *storageDetails) validate(log logging.Logger, engineCount int, minNrSSDs int) error {
	log.Debugf("numa to pmem mappings: %v", sd.numaPMems)
	if len(sd.numaPMems) < engineCount {
		return errors.Errorf(errInsufNrPMemGroups, sd.numaPMems, engineCount, len(sd.numaPMems))
	}

	if minNrSSDs == 0 {
		// set empty ssd lists and skip validation
		log.Debug("nvme disabled, skip validation")

		for nn := 0; nn < engineCount; nn++ {
			sd.numaSSDs[nn] = []string{}
		}

		return nil
	}

	for nn := 0; nn < engineCount; nn++ {
		ssds, exists := sd.numaSSDs[nn]
		if !exists {
			sd.numaSSDs[nn] = []string{} // populate empty lists for missing entries
		}
		log.Debugf("ssds bound to numa %d: %v", nn, ssds)

		if len(ssds) < minNrSSDs {
			return errors.Errorf(errInsufNrSSDs, nn, minNrSSDs, len(ssds))
		}
	}

	return nil
}

// getStorageDetails retrieves mappings of NUMA node to PMem and NVMe SSD
// devices.
//
// Returns storage details struct or host error response and outer error.
func getStorageDetails(ctx context.Context, req ConfigGenerateReq, engineCount int) (*storageDetails, *HostErrorsResp, error) {
	if engineCount < 1 {
		return nil, nil, errors.Errorf(errInvalNrEngines, 1, engineCount)
	}

	hostErrs, storageSet, err := getStorageSet(ctx, req.Log, req.HostList, req.Client)
	if err != nil {
		return nil, hostErrs, err
	}

	sd := &storageDetails{
		numaPMems: mapPMems(storageSet.HostStorage.ScmNamespaces),
		numaSSDs:  mapSSDs(storageSet.HostStorage.NvmeDevices),
	}
	if err := sd.validate(req.Log, engineCount, req.MinNrSSDs); err != nil {
		return nil, nil, err
	}

	return sd, nil, nil
}

func calcHelpers(log logging.Logger, targets, cores int) int {
	helpers := cores - targets - 1
	if helpers <= 1 {
		return helpers
	}

	if helpers > targets {
		log.Debugf("adjusting num helpers (%d) to < num targets (%d), new: %d",
			helpers, targets, targets-1)

		return targets - 1
	}

	return helpers
}

type coreCounts struct {
	nrTgts  int
	nrHlprs int
}

// numaCoreCountsMap is an alias for a map of NUMA node ID to calculate target
// and helper core counts.
type numaCoreCountsMap map[int]*coreCounts

// checkCPUs validates and returns recommended values for I/O service and
// offload thread counts.
//
// The target count should be a multiplier of the number of SSDs and typically
// daos gets the best performance with 16x targets per I/O Engine so target
// count will typically be between 12 and 20.
//
// Validate number of targets + 1 cores are available per IO engine, not
// usually a problem as sockets normally have at least 18 cores.
//
// Create helper threads for the remaining available cores, e.g. with 24 cores,
// allocate 7 helper threads. Number of helper threads should never be more than
// number of targets.
func checkCPUs(log logging.Logger, numSSDs, numaCoreCount int) (*coreCounts, error) {
	var numTargets int
	if numSSDs == 0 {
		numTargets = defaultTargetCount
		if numTargets >= numaCoreCount {
			return &coreCounts{
				nrTgts:  numaCoreCount - 1,
				nrHlprs: 0,
			}, nil
		}

		return &coreCounts{
			nrTgts:  numTargets,
			nrHlprs: calcHelpers(log, numTargets, numaCoreCount),
		}, nil
	}

	if numSSDs >= numaCoreCount {
		return nil, errors.Errorf("need more cores than ssds, got %d want %d",
			numaCoreCount, numSSDs)
	}

	for tgts := numSSDs; tgts < numaCoreCount; tgts += numSSDs {
		numTargets = tgts
	}

	log.Debugf("%d targets assigned with %d ssds", numTargets, numSSDs)

	return &coreCounts{
		nrTgts:  numTargets,
		nrHlprs: calcHelpers(log, numTargets, numaCoreCount),
	}, nil
}

// getCPUDetails retrieves recommended values for I/O service and offload
// threads suitable for the server config file.
//
// Returns core counts struct or error.
func getCPUDetails(log logging.Logger, numaSSDs numaSSDsMap, coresPerNuma int) (numaCoreCountsMap, error) {
	if coresPerNuma < 1 {
		return nil, errors.Errorf(errInvalNrCores, coresPerNuma)
	}

	numaCoreCounts := make(numaCoreCountsMap)
	for numaID, ssds := range numaSSDs {
		coreCounts, err := checkCPUs(log, len(ssds), coresPerNuma)
		if err != nil {
			return nil, err
		}
		numaCoreCounts[numaID] = coreCounts
	}

	return numaCoreCounts, nil
}

func defaultEngineCfg(idx int) *engine.Config {
	return engine.NewConfig().
		WithTargetCount(defaultTargetCount).
		WithLogFile(fmt.Sprintf("%s.%d.log", defaultEngineLogFile, idx)).
		WithScmClass(storage.ScmClassDCPM.String()).
		WithBdevClass(storage.BdevClassNvme.String()).
		WithBdevDeviceList([]string{}...)
}

// genConfig generates server config file from details of available network,
// storage and CPU hardware.
func genConfig(log logging.Logger, accessPoints []string, nd *networkDetails, sd *storageDetails, ccs numaCoreCountsMap) (*config.Server, error) {
	// basic sanity checks
	if nd.engineCount == 0 {
		return nil, errors.Errorf(errInvalNrEngines, 1, 0)
	}
	if len(nd.numaIfaces) < nd.engineCount {
		return nil, errors.Errorf(errInsufNrIfaces, "", nd.engineCount,
			len(nd.numaIfaces), nd.numaIfaces)
	}
	if len(sd.numaPMems) < nd.engineCount {
		return nil, errors.Errorf(errInsufNrPMemGroups, sd.numaPMems, nd.engineCount,
			len(sd.numaPMems))
	}
	if len(sd.numaSSDs) < nd.engineCount {
		return nil, errors.New("invalid number of ssd groups") // shouldn't happen
	}
	if len(ccs) < nd.engineCount {
		return nil, errors.New("invalid number of core count groups") // shouldn't happen
	}

	engines := make([]*engine.Config, 0, nd.engineCount)
	for nn := 0; nn < nd.engineCount; nn++ {
		engineCfg := defaultEngineCfg(nn).
			WithScmMountPoint(fmt.Sprintf("%s%d", scmMountPrefix, nn)).
			WithScmDeviceList(sd.numaPMems[nn][0]).
			WithBdevDeviceList(sd.numaSSDs[nn]...).
			WithTargetCount(ccs[nn].nrTgts).
			WithHelperStreamCount(ccs[nn].nrHlprs)

		pnn := uint(nn)
		engineCfg.Fabric = engine.FabricConfig{
			Provider:       nd.numaIfaces[nn].Provider,
			Interface:      nd.numaIfaces[nn].Device,
			InterfacePort:  int(defaultFiPort + (nn * defaultFiPortInterval)),
			PinnedNumaNode: &pnn,
		}

		engines = append(engines, engineCfg)
	}

	cfg := config.DefaultServer().
		WithAccessPoints(accessPoints...).
		WithFabricProvider(engines[0].Fabric.Provider).
		WithEngines(engines...).
		WithControlLogFile(defaultControlLogFile)

	return cfg, cfg.Validate(log)
}
