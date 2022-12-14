//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"fmt"
	"math"
	"math/bits"
	"sort"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/hardware"
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
	minDMABuffer          = 1024

	errUnsupNetDevClass  = "unsupported net dev class in request: %s"
	errInsufNrIfaces     = "insufficient matching fabric interfaces, want %d got %d %v"
	errInsufNrPMemGroups = "insufficient number of pmem device numa groups %v, want %d got %d"
	errInsufNrSSDGroups  = "insufficient number of ssd device numa groups %v, want %d got %d"
	errInsufNrSSDs       = "insufficient number of ssds for numa %d, want %d got %d"
	errInvalNrCores      = "invalid number of cores for numa %d, want at least 2"
	errInsufNrProvGroups = "none of the provider-ifaces sets match the numa node sets " +
		"that meet storage requirements"
)

var errNoNuma = errors.New("zero numa nodes reported on hosts")

type (
	// ConGenerateReq contains the inputs for the request.
	ConfGenerateReq struct {
		NrEngines    int
		MinNrSSDs    int
		NetClass     hardware.NetDevClass
		AccessPoints []string
		Log          logging.Logger
	}

	// ConfGenerateResp contains the generated server config.
	ConfGenerateResp struct {
		config.Server
	}

	// ConfGenerateRemoteReq adds connectivity related fields to base request.
	ConfGenerateRemoteReq struct {
		ConfGenerateReq
		unaryRequest
		msRequest
		Client UnaryInvoker
	}

	// ConfGenerateRemoteResp wraps the ConfGenerateResp.
	ConfGenerateRemoteResp struct {
		ConfGenerateResp
	}

	// ConfGenerateError implements the error interface and contains a set of host-specific
	// errors encountered while attempting to generate a configuration.
	ConfGenerateError struct {
		HostErrorsResp
	}
)

func (cge *ConfGenerateError) Error() string {
	return cge.Errors().Error()
}

// GetHostErrors returns the wrapped HostErrorsMap.
func (cge *ConfGenerateError) GetHostErrors() HostErrorsMap {
	return cge.HostErrors
}

// IsConfGenerateError returns true if the provided error is a *ConfGenerateError.
func IsConfGenerateError(err error) bool {
	_, ok := errors.Cause(err).(*ConfGenerateError)
	return ok
}

// DefaultEngineCfg is a standard baseline for the config generate to start from.
func DefaultEngineCfg(idx int) *engine.Config {
	return engine.NewConfig().
		WithTargetCount(defaultTargetCount).
		WithLogFile(fmt.Sprintf("%s.%d.log", defaultEngineLogFile, idx))
}

// ConfGenerate derives an optimal server config file from details of network, storage and CPU
// hardware by evaluating affinity matches for NUMA node combinations.
//
// TODO DAOS-11859: When enabling tmpfs SCM, enumerate them in the same map NumaSCMs.
func ConfGenerate(req ConfGenerateReq, newEngineCfg newEngineCfgFn, hf *HostFabric, hs *HostStorage) (*ConfGenerateResp, error) {
	// process host fabric scan results to retrieve network details
	nd, err := getNetworkDetails(req.Log, req.NetClass, hf)
	if err != nil {
		return nil, err
	}

	// process host storage scan results to retrieve storage details
	sd, err := getStorageDetails(req.Log, hs)
	if err != nil {
		return nil, err
	}

	// evaluate device affinities to enforce locality constraints
	nodeSet, err := filterDevicesByAffinity(req.Log, req.NrEngines, req.MinNrSSDs, nd, sd)
	if err != nil {
		return nil, err
	}

	// populate engine configs with storage and network devices
	ecs, err := genEngineConfigs(req.Log, req.MinNrSSDs, newEngineCfg, nodeSet, nd, sd)
	if err != nil {
		return nil, err
	}

	// calculate service and helper thread counts
	tc, err := getThreadCounts(req.Log, ecs[0], nd.NumaCoreCount)
	if err != nil {
		return nil, err
	}

	// populate server config using engine configs
	sc, err := genServerConfig(req.Log, req.AccessPoints, ecs, sd.HugePageSize, tc)
	if err != nil {
		return nil, err
	}

	resp := ConfGenerateResp{Server: *sc}
	return &resp, nil
}

// ConfGenerateRemote calls ConfGenerate after validating a homogeneous hardware setup across
// remote hosts. Returns API response or error.
func ConfGenerateRemote(ctx context.Context, req ConfGenerateRemoteReq) (*ConfGenerateRemoteResp, error) {
	req.Log.Debugf("ConfGenerateRemote called with request %+v", req)

	if len(req.HostList) == 0 {
		return nil, errors.New("no hosts specified")
	}

	if len(req.AccessPoints) == 0 {
		return nil, errors.New("no access points specified")
	}

	ns, err := getNetworkSet(ctx, req.Log, req.HostList, req.Client)
	if err != nil {
		return nil, err
	}

	ss, err := getStorageSet(ctx, req.Log, req.HostList, req.Client)
	if err != nil {
		return nil, err
	}

	resp, err := ConfGenerate(req.ConfGenerateReq, DefaultEngineCfg, ns.HostFabric,
		ss.HostStorage)
	if err != nil {
		return nil, err
	}

	remResp := ConfGenerateRemoteResp{ConfGenerateResp: *resp}
	return &remResp, nil
}

// getNetworkSet retrieves the result of network scan over host list and verifies that there is
// only a single network set in response which indicates that network hardware setup is homogeneous
// across all hosts.  Return host errors, network scan results for the host set or error.
func getNetworkSet(ctx context.Context, log logging.Logger, hostList []string, client UnaryInvoker) (*HostFabricSet, error) {
	log.Debugf("fetching host fabric info on hosts %v", hostList)

	scanReq := &NetworkScanReq{
		Provider: "all", // explicitly request all providers
	}
	scanReq.SetHostList(hostList)

	scanResp, err := NetworkScan(ctx, client, scanReq)
	if err != nil {
		return nil, err
	}

	if len(scanResp.GetHostErrors()) > 0 {
		return nil, &ConfGenerateError{HostErrorsResp: scanResp.HostErrorsResp}
	}

	// verify homogeneous network
	switch len(scanResp.HostFabrics) {
	case 0:
		return nil, errors.New("no host responses")
	case 1:
		// success
	default:
		// more than one means non-homogeneous hardware
		log.Info("Heterogeneous network hardware configurations detected, " +
			"cannot proceed. The following sets of hosts have different " +
			"network hardware:")
		for _, hns := range scanResp.HostFabrics {
			log.Info(hns.HostSet.String())
		}

		return nil, errors.New("network hardware not consistent across hosts")
	}

	networkSet := scanResp.HostFabrics[scanResp.HostFabrics.Keys()[0]]

	log.Debugf("Network hardware is consistent for hosts %s:\n\t%v",
		networkSet.HostSet, networkSet.HostFabric.Interfaces)

	return networkSet, nil
}

// Return ordered list of keys from int key map.
func intMapKeys(i interface{}) (keys []int) {
	m, ok := i.(map[int]interface{})
	if !ok {
		panic("map doesn't have int key type")
	}

	for k := range m {
		keys = append(keys, k)
	}
	sort.Ints(keys)

	return keys
}

// numaNetIfaceMap is an alias for a map of NUMA node ID to optimal fabric network interface.
type numaNetIfaceMap map[int]*HostFabricInterface

func (nnim numaNetIfaceMap) keys() (keys []int) {
	for k := range nnim {
		keys = append(keys, k)
	}
	sort.Ints(keys)

	return
}

// providerIfaceMap is an alias for a provider-to-numaNetIfaceMap map.
type providerIfaceMap map[string]numaNetIfaceMap

// add network device to bucket corresponding to provider and NUMA binding. Do not add if there is
// an existing entry as the interfaces are processed in descending order of performance (best
// first).
func (pis providerIfaceMap) add(iface *HostFabricInterface) {
	nn := int(iface.NumaNode)
	if _, exists := pis[iface.Provider]; !exists {
		pis[iface.Provider] = make(numaNetIfaceMap)
	}
	if _, exists := pis[iface.Provider][nn]; exists {
		return // already have interface for this NUMA
	}
	pis[iface.Provider][nn] = iface
}

// parseInterfaces processes network devices in scan result, adding to a provider bucket when: IF
// class == (ether OR infiniband) AND requested_class == (ANY OR <class>).  Returns when network
// devices matching NetDevClass criteria have been assigned to provider bucket with NUMA node
// mappings.
func (pim providerIfaceMap) fromFabric(reqClass hardware.NetDevClass, ifaces []*HostFabricInterface) error {
	if pim == nil {
		return errors.Errorf("%T receiver is nil", pim)
	}

	switch reqClass {
	case hardware.NetDevAny, hardware.Ether, hardware.Infiniband:
	default:
		return errors.Errorf(errUnsupNetDevClass, reqClass.String())
	}

	// sort network interfaces by priority to get best available
	sort.Slice(ifaces, func(i, j int) bool {
		return ifaces[i].Priority < ifaces[j].Priority
	})

	for _, iface := range ifaces {
		switch iface.NetDevClass {
		case hardware.Ether, hardware.Infiniband:
			switch reqClass {
			case hardware.NetDevAny, iface.NetDevClass:
			default:
				continue // iface class not requested
			}
		default:
			continue // iface class unsupported
		}

		pim.add(iface)
	}

	return nil
}

type networkDetails struct {
	NumaCount      int
	NumaCoreCount  int
	ProviderIfaces providerIfaceMap
	NumaIfaces     numaNetIfaceMap
}

// getNetworkDetails retrieves fabric network interfaces that can be used in server config file.
// Return interfaces mapped first by NUMA then provider and per-NUMA core count.
func getNetworkDetails(log logging.Logger, ndc hardware.NetDevClass, hf *HostFabric) (*networkDetails, error) {
	if hf == nil {
		return nil, errors.New("nil HostFabric")
	}

	provIfaces := make(providerIfaceMap)
	if err := provIfaces.fromFabric(ndc, hf.Interfaces); err != nil {
		return nil, err
	}
	log.Debugf("numa nodes: %d, numa core count: %d, available interfaces %v", hf.NumaCount,
		hf.CoresPerNuma, provIfaces)

	return &networkDetails{
		NumaCount:      int(hf.NumaCount),
		NumaCoreCount:  int(hf.CoresPerNuma),
		ProviderIfaces: provIfaces,
	}, nil
}

// getStorageSet retrieves the result of storage scan over host list and verifies that there is
// only a single storage set in response which indicates that storage hardware setup is homogeneous
// across all hosts.  Filter NVMe storage scan so only NUMA affinity and PCI address is taking into
// account by supplying NvmeBasic flag in scan request. This enables configuration to work with
// different combinations of SSD models.  Return host errors, storage scan results for the host set
// or error.
func getStorageSet(ctx context.Context, log logging.Logger, hostList []string, client UnaryInvoker) (*HostStorageSet, error) {
	log.Debugf("fetching host storage info on hosts %v", hostList)

	scanReq := &StorageScanReq{NvmeBasic: true}
	scanReq.SetHostList(hostList)

	scanResp, err := StorageScan(ctx, client, scanReq)
	if err != nil {
		return nil, err
	}
	if len(scanResp.GetHostErrors()) > 0 {
		return nil, &ConfGenerateError{HostErrorsResp: scanResp.HostErrorsResp}
	}

	// verify homogeneous storage
	switch len(scanResp.HostStorage) {
	case 0:
		return nil, errors.New("no host responses")
	case 1:
		// success
	default:
		// more than one means non-homogeneous hardware
		log.Info("Heterogeneous storage hardware configurations detected, " +
			"cannot proceed. The following sets of hosts have different " +
			"storage hardware:")
		for _, hss := range scanResp.HostStorage {
			log.Info(hss.HostSet.String())
		}

		return nil, errors.New("storage hardware not consistent across hosts")
	}

	storageSet := scanResp.HostStorage[scanResp.HostStorage.Keys()[0]]

	log.Debugf("Storage hardware is consistent for hosts %s:\n\t%s\n\t%s",
		storageSet.HostSet.String(), storageSet.HostStorage.ScmNamespaces.Summary(),
		storageSet.HostStorage.NvmeDevices.Summary())

	return storageSet, nil
}

// numaSCMsMap is an alias for a map of NUMA node ID to slice of string sorted PMem block device
// paths.
type numaSCMsMap map[int]sort.StringSlice

func (npm numaSCMsMap) keys() (keys []int) {
	for k := range npm {
		keys = append(keys, k)
	}
	sort.Ints(keys)

	return
}

// mapPMems maps NUMA node ID to pmem block device paths, sort paths to attempt selection of
// desired devices if named appropriately in the case that multiple devices exist for a given NUMA
// node.
func (npm numaSCMsMap) fromSCM(nss storage.ScmNamespaces) error {
	if npm == nil {
		return errors.Errorf("%T receiver is nil", npm)
	}

	for _, ns := range nss {
		nn := int(ns.NumaNode)
		npm[nn] = append(npm[nn], fmt.Sprintf("%s/%s", scmBdevDir, ns.BlockDevice))
	}
	for _, pms := range npm {
		pms.Sort()
	}

	return nil
}

// numSSDsMap is an alias for a map of NUMA node ID to slice of NVMe SSD PCI addresses.
type numaSSDsMap map[int]*hardware.PCIAddressSet

func (nsm numaSSDsMap) keys() (keys []int) {
	for k := range nsm {
		keys = append(keys, k)
	}
	sort.Ints(keys)

	return
}

// mapSSDs maps NUMA node ID to NVMe SSD PCI address set.
func (nsm numaSSDsMap) fromNVMe(ssds storage.NvmeControllers) error {
	if nsm == nil {
		return errors.Errorf("%T receiver is nil", nsm)
	}

	for _, ssd := range ssds {
		nn := int(ssd.SocketID)
		if _, exists := nsm[nn]; exists {
			if err := nsm[nn].AddStrings(ssd.PciAddr); err != nil {
				return err
			}
			continue
		}

		newAddrSet, err := hardware.NewPCIAddressSetFromString(ssd.PciAddr)
		if err != nil {
			return err
		}
		nsm[nn] = newAddrSet
	}

	return nil
}

type storageDetails struct {
	HugePageSize int
	NumaSCMs     numaSCMsMap
	NumaSSDs     numaSSDsMap
}

// getStorageDetails retrieves mappings of NUMA node to PMem and NVMe SSD devices.  Returns storage
// details struct or host error response and outer error.
func getStorageDetails(log logging.Logger, hs *HostStorage) (*storageDetails, error) {
	if hs == nil {
		return nil, errors.New("nil HostStorage")
	}

	numaSSDs := make(numaSSDsMap)
	if err := numaSSDs.fromNVMe(hs.NvmeDevices); err != nil {
		return nil, errors.Wrap(err, "mapping ssd addresses to numa node")
	}

	numaSCMDevs := make(numaSCMsMap)
	if err := numaSCMDevs.fromSCM(hs.ScmNamespaces); err != nil {
		return nil, errors.Wrap(err, "mapping scm block device names to numa node")
	}

	return &storageDetails{
		NumaSCMs:     numaSCMDevs,
		NumaSSDs:     numaSSDs,
		HugePageSize: hs.HugePageInfo.PageSizeKb,
	}, nil
}

// Filters PMem and SSD groups to include only the NUMA IDs that have sufficient number of devices
// with appropriate affinity. Returns error if not enough satisfied NUMA ID groupings for required
// engine count.
//
// TODO DAOS-9932: set bdev_class to ram if --use-tmpfs-scm is set
func checkNvmeAffinity(log logging.Logger, engineCount, minNrSSDs int, sd *storageDetails) error {
	log.Debugf("numa to pmem mappings: %v", sd.NumaSCMs)
	log.Debugf("numa to nvme mappings: %v", sd.NumaSSDs)

	if len(sd.NumaSCMs) < engineCount {
		return errors.Errorf(errInsufNrPMemGroups, sd.NumaSCMs, engineCount, len(sd.NumaSCMs))
	}

	if minNrSSDs == 0 {
		log.Debug("nvme disabled, skip validation")

		// set empty ssd lists for relevant numa ids
		sd.NumaSSDs = make(numaSSDsMap)
		for numaID := range sd.NumaSCMs {
			sd.NumaSSDs[numaID] = hardware.MustNewPCIAddressSet()
		}

		return nil
	}

	var pass, fail []int
	for numaID := range sd.NumaSCMs {
		if sd.NumaSSDs[numaID].Len() >= minNrSSDs {
			pass = append(pass, numaID)
		} else {
			fail = append(fail, numaID)
		}
	}

	switch {
	case len(pass) > 0 && len(fail) > 0:
		log.Debugf("ssd requirements met for numa ids %v but not for %v", pass, fail)
	case len(pass) > 0:
		log.Debugf("ssd requirements met for numa ids %v", pass)
	default:
		log.Debugf("ssd requirements not met for numa ids %v", fail)
	}

	if len(pass) < engineCount {
		// fail if the number of passing numa id groups is less than required
		log.Errorf("ssd-to-numa mapping validation failed, not enough numaID groupings "+
			"satisfy SSD requirements (%d per-engine) to meet the number of required "+
			"engines (%d)", minNrSSDs, engineCount)

		// print first failing numa id details in returned error
		for _, numaID := range sd.NumaSCMs.keys() {
			if sd.NumaSSDs[numaID].Len() >= minNrSSDs {
				continue
			}
			return errors.Errorf(errInsufNrSSDs, numaID, minNrSSDs,
				sd.NumaSSDs[numaID].Len())
		}

		// unexpected operation, program flow should not reach here
		return errors.New("no pmem-to-numa mappings")
	}

	// truncate storage maps to remove entries that don't meet requirements
	for _, idx := range fail {
		delete(sd.NumaSCMs, idx)
		delete(sd.NumaSSDs, idx)
	}
	log.Debugf("storage validation passed, scm: %+v, nvme: %+v", sd.NumaSCMs,
		sd.NumaSSDs)

	return nil
}

// returns all combinations of n size in set
func combinations(set []int, n int) (sss [][]int) {
	length := uint(len(set))

	if n > len(set) {
		n = len(set)
	}

	// iterate through possible combinations and assign subsets
	for ssBits := 1; ssBits < (1 << length); ssBits++ {
		if n > 0 && bits.OnesCount(uint(ssBits)) != n {
			continue
		}

		var ss []int

		for obj := uint(0); obj < length; obj++ {
			// is obj in subset?
			if (ssBits>>obj)&1 == 1 {
				ss = append(ss, set[obj])
			}
		}
		sss = append(sss, ss)
	}

	for _, ss := range sss {
		sort.Ints(ss)
	}

	// sort slices by first element
	sort.Slice(sss, func(i, j int) bool {
		if len(sss[i]) == 0 && len(sss[j]) == 0 {
			return false
		}
		if len(sss[i]) == 0 || len(sss[j]) == 0 {
			return len(sss[i]) == 0
		}

		return sss[i][0] < sss[j][0]
	})

	return sss
}

// returns true if sub is part of super
func isSubset(sub, super []int) bool {
	set := make(map[int]int)
	for _, val := range super {
		set[val] += 1
	}

	for _, val := range sub {
		if set[val] == 0 {
			return false
		}
		set[val] -= 1
	}

	return true
}

type rating struct {
	prioSum  int
	provider string
}

func getFabricScores(log logging.Logger, nodeSets [][]int, nd *networkDetails) ([]*rating, error) {
	// return combined interface score for each numa node set
	scores := make([]*rating, len(nodeSets))

	for idx, ns := range nodeSets {
		if len(ns) == 0 {
			return nil, errors.Errorf("unexpected empty numa node set at index %d", idx)
		}

		// if a input numa node set is a subset of the node set supported on fabric interfaces
		// for a specific provider, record it as a matching provider
		var matchingProviders []string
		for p, numaIfaces := range nd.ProviderIfaces {
			if isSubset(ns, numaIfaces.keys()) {
				matchingProviders = append(matchingProviders, p)
			}
		}

		if len(matchingProviders) == 0 {
			log.Debugf("no providers with supported fabric ifaces across numa set %v", ns)
			continue
		}
		log.Debugf("providers %v supported across numa set %v", matchingProviders, ns)

		// for each matching provider, sum priority scores for each node's first iface
		// and select provider with lowest sum priority for this node set
		var bestProvider string
		var bestScore int = math.MaxInt32
		for _, p := range matchingProviders {
			var score int
			for _, n := range ns {
				ni := nd.ProviderIfaces[p][n]
				if ni == nil {
					return nil, errors.Errorf("nil iface for provider %s, numa %d",
						p, n)
				}
				score += int(ni.Priority)
			}
			if score < bestScore {
				bestScore = score
				bestProvider = p
			}
		}

		if bestProvider == "" || bestScore == math.MaxUint32 {
			// unexpected, score should have been calculated by this point
			return nil, errors.Errorf("fabric score not calculated for numa node set %v",
				ns)
		}

		scores[idx] = &rating{
			prioSum:  bestScore,
			provider: bestProvider,
		}

		log.Debugf("fabric score for numa node set %v: %+v", ns, *scores[idx])
	}

	return scores, nil
}

func nodeSetToMap(ns []int) map[int]bool {
	nm := make(map[int]bool)
	for _, ni := range ns {
		nm[ni] = true
	}

	return nm
}

// Trim device details to fit the number of engines required.
func trimStorageDeviceMaps(nodeSet []int, sd *storageDetails) {
	nodesToUse := nodeSetToMap(nodeSet)
	for numaID := range sd.NumaSCMs {
		if !nodesToUse[numaID] {
			delete(sd.NumaSCMs, numaID)
		}
	}
	for numaID := range sd.NumaSSDs {
		if !nodesToUse[numaID] {
			delete(sd.NumaSSDs, numaID)
		}
	}
}

// Find min nr ssds across numa node set and first node with that number.
func lowestCommonNrSSDs(nodeSet []int, numaSSDs numaSSDsMap) (int, error) {
	nodesToUse := nodeSetToMap(nodeSet)
	minNrSSDs := math.MaxUint32

	for numaID, ssds := range numaSSDs {
		if nodesToUse[numaID] && ssds.Len() < minNrSSDs {
			minNrSSDs = ssds.Len()
		}
	}

	if minNrSSDs == math.MaxUint32 {
		return 0, errors.New("lowest common number of ssds could not be calculated")
	}

	return minNrSSDs, nil
}

// Generate scores for interfaces supporting a specific provider across a set of NUMA nodes whose
// length matches engineCount. Score is sum of interface priority values. Choose lowest score.
func chooseEngineAffinity(log logging.Logger, engineCount int, nd *networkDetails, sd *storageDetails) ([]int, error) {
	nodes := sd.NumaSCMs.keys()

	// retrieve numa node combinations that meet storage requirements for an engine config
	nodeSets := combinations(nodes, engineCount)
	if len(nodeSets) == 0 {
		return nil, errors.New("no numa node sets found in scm map")
	}
	log.Debugf("numasets for possible engine configs: %v (from superset %v)", nodeSets, nodes)

	fabricScores, err := getFabricScores(log, nodeSets, nd)
	if err != nil {
		return nil, err
	}

	// choose the numa node set with the lowest fabric priority score (lower is better), if
	// score is equal, choose nodes with max lowest-common number of ssds
	var bestNumaSet []int
	var bestFabric *rating
	for idx, fs := range fabricScores {
		switch {
		case fs == nil:
			continue
		case bestFabric == nil:
			// first entrant, select winner
		case fs.prioSum > bestFabric.prioSum:
			// higher score, skip entrant
			continue
		case fs.prioSum < bestFabric.prioSum:
			// new winning score, select winner
		default:
			// equal lowest score, choose set with max lowest common number of ssds
			curBestNrSSDs, err := lowestCommonNrSSDs(bestNumaSet, sd.NumaSSDs)
			if err != nil {
				return nil, err
			}
			newBestNrSSDs, err := lowestCommonNrSSDs(nodeSets[idx], sd.NumaSSDs)
			if err != nil {
				return nil, err
			}
			if newBestNrSSDs <= curBestNrSSDs {
				continue
			}
		}

		bestFabric = fs
		bestNumaSet = nodeSets[idx]
	}

	if len(bestNumaSet) == 0 || bestFabric == nil {
		return nil, errors.New(errInsufNrProvGroups)
	}
	log.Debugf("fabric provider %s chosen on numa nodes %v", bestFabric.provider, bestNumaSet)

	// populate specific fabric interfaces to use for each numa node
	nd.NumaIfaces = make(numaNetIfaceMap)
	for _, nn := range bestNumaSet {
		iface := nd.ProviderIfaces[bestFabric.provider][nn]
		if iface != nil {
			nd.NumaIfaces[nn] = iface
		}
	}
	nd.ProviderIfaces = nil

	// trim any storage device groupings for numa ids that are unsuitable for engine configs
	trimStorageDeviceMaps(bestNumaSet, sd)

	// sanity check that the number of satisfied numa id groups matches number of engines required
	if len(nd.NumaIfaces) != engineCount {
		return nil, errors.Errorf(errInsufNrIfaces, engineCount, len(nd.NumaIfaces),
			nd.NumaIfaces)
	}
	if len(sd.NumaSCMs) != engineCount {
		return nil, errors.Errorf(errInsufNrPMemGroups, sd.NumaSCMs, engineCount,
			len(sd.NumaSCMs))
	}
	if len(sd.NumaSSDs) != engineCount {
		return nil, errors.Errorf(errInsufNrSSDGroups, sd.NumaSSDs, engineCount,
			len(sd.NumaSSDs))
	}

	sort.Ints(bestNumaSet)
	return bestNumaSet, nil
}

func filterDevicesByAffinity(log logging.Logger, nrEngines, minNrSSDs int, nd *networkDetails, sd *storageDetails) ([]int, error) {
	// if unset, assign number of engines based on number of NUMA nodes
	if nrEngines == 0 {
		nrEngines = nd.NumaCount
	}
	if nrEngines == 0 {
		return nil, errNoNuma
	}

	log.Debugf("attempting to generate config with %d engines", nrEngines)

	if err := checkNvmeAffinity(log, nrEngines, minNrSSDs, sd); err != nil {
		return nil, err
	}

	nodeSet, err := chooseEngineAffinity(log, nrEngines, nd, sd)
	if err != nil {
		return nil, err
	}

	if len(nodeSet) == 0 {
		return nil, errors.New("generated config should have at least one engine")
	}

	return nodeSet, nil
}

func correctSSDCounts(log logging.Logger, minNrSSDs int, sd *storageDetails) error {
	if minNrSSDs == 0 {
		// the use of ssds has been intentionally disabled, skip corrections
		return nil
	}

	// calculate ssd count lowest value across numa nodes
	minSSDsInCfg, err := lowestCommonNrSSDs(sd.NumaSSDs.keys(), sd.NumaSSDs)
	if err != nil {
		return err
	}
	log.Debugf("selecting %d ssds per engine (lowest value across engines)", minSSDsInCfg)

	// second pass to apply corrections
	for numaID := range sd.NumaSSDs {
		ssds := sd.NumaSSDs[numaID]
		if ssds.Len() > minSSDsInCfg {
			log.Debugf("larger number of SSDs (%d) on NUMA-%d than the lowest common "+
				"number across all relevant NUMA nodes (%d), configuration is "+
				"not balanced", ssds.Len(), numaID, minSSDsInCfg)

			if ssds.HasVMD() {
				// Not currently possible to restrict the number of backing devices
				// used behind a VMD so refuse to generate config.
				return FaultConfigVMDImbalance
			}

			// restrict ssds used so that number is equal across engines
			log.Debugf("only using %d SSDs from NUMA-%d from an available %d",
				minSSDsInCfg, numaID, ssds.Len())

			ssdAddrs := ssds.Strings()[:minSSDsInCfg]
			sd.NumaSSDs[numaID] = hardware.MustNewPCIAddressSet(ssdAddrs...)
		}

		if ssds.HasVMD() {
			// If addresses are for VMD backing devices, convert to the logical VMD
			// endpoint address as this is what is expected in the server config.
			newAddrSet, err := ssds.BackingToVMDAddresses(log)
			if err != nil {
				return errors.Wrap(err, "converting backing addresses to vmd")
			}
			sd.NumaSSDs[numaID] = newAddrSet
		}
	}

	return nil
}

type newEngineCfgFn func(int) *engine.Config

func genEngineConfigs(log logging.Logger, minNrSSDs int, newEngineCfg newEngineCfgFn, nodeSet []int, nd *networkDetails, sd *storageDetails) ([]*engine.Config, error) {
	// first sanity check required component groups
	for _, numaID := range nodeSet {
		if len(sd.NumaSCMs[numaID]) == 0 {
			return nil, errors.Errorf("no scm device found for numa %d", numaID)
		}
		if _, exists := sd.NumaSSDs[numaID]; !exists {
			return nil, errors.Errorf("no ssds found for numa %d", numaID)
		}
		if _, exists := nd.NumaIfaces[numaID]; !exists {
			return nil, errors.Errorf("no fabric interface found for numa %d", numaID)
		}
	}

	// make ssd counts consistent across numa groupings
	if err := correctSSDCounts(log, minNrSSDs, sd); err != nil {
		return nil, err
	}

	cfgs := make([]*engine.Config, 0, len(nodeSet))

	for _, numaID := range nodeSet {
		ssds := sd.NumaSSDs[numaID]
		iface := nd.NumaIfaces[numaID]

		tiers := storage.TierConfigs{
			storage.NewTierConfig().
				WithStorageClass(storage.ClassDcpm.String()).
				WithScmMountPoint(fmt.Sprintf("%s%d", scmMountPrefix, numaID)).
				WithScmDeviceList(sd.NumaSCMs[numaID][0]),
		}
		// TODO DAOS-11859: Assign SSDs to multiple tiers for MD-on-SSD
		if ssds.Len() > 0 {
			tiers = append(tiers, storage.NewTierConfig().
				WithStorageClass(storage.ClassNvme.String()).
				WithBdevDeviceList(ssds.Strings()...))
		}
		cfg := newEngineCfg(len(cfgs)).WithStorage(tiers...)

		pnn := uint(numaID)
		cfg.PinnedNumaNode = &pnn
		cfg.Fabric = engine.FabricConfig{
			Provider:      iface.Provider,
			Interface:     iface.Device,
			InterfacePort: int(defaultFiPort + (numaID * defaultFiPortInterval)),
		}
		if err := cfg.SetNUMAAffinity(pnn); err != nil {
			return nil, errors.Wrapf(err, "setting numa %d affinity on engine config",
				pnn)
		}

		cfgs = append(cfgs, cfg)
	}

	return cfgs, nil
}

type threadCounts struct {
	nrTgts  int
	nrHlprs int
}

// getThreadCounts validates and returns recommended values for I/O service and offload thread
// counts. The target count should be a multiplier of the number of SSDs and typically daos gets
// the best performance with 16x targets per I/O Engine so target count will typically be between
// 12 and 20.  Check number of targets + 1 cores are available per IO engine, not usually a problem
// as sockets normally have at least 18 cores.  Create helper threads for the remaining available
// cores, e.g. with 24 cores, allocate 7 helper threads. Number of helper threads should never be
// more than number of targets.
func getThreadCounts(log logging.Logger, ec *engine.Config, numaCoreCount int) (*threadCounts, error) {
	if ec == nil {
		return nil, errors.Errorf("nil %T parameter", ec)
	}
	if numaCoreCount < 2 {
		return nil, errors.Errorf(errInvalNrCores, numaCoreCount)
	}

	// TODO DAOS-11859: Do we want to calculate based on data role SSDs only?
	var numTargets int
	numSSDs := ec.Storage.Tiers.NVMeBdevs().Len()
	if numSSDs == 0 {
		numTargets = defaultTargetCount
		if numTargets >= numaCoreCount {
			numTargets = numaCoreCount - 1
		}
		log.Debugf("nvme disabled, %d targets assigned and 0 helper threads",
			numTargets)
		return &threadCounts{
			nrTgts:  numTargets,
			nrHlprs: 0,
		}, nil
	} else if numSSDs >= numaCoreCount {
		return nil, errors.Errorf("need more cores than ssds, got %d want %d",
			numaCoreCount, numSSDs)
	} else {
		for tgts := numSSDs; tgts < numaCoreCount; tgts += numSSDs {
			numTargets = tgts
		}
	}

	log.Debugf("%d targets assigned with %d ssds", numTargets, numSSDs)

	numHelpers := numaCoreCount - numTargets - 1
	if numHelpers > 1 && numHelpers > numTargets {
		log.Debugf("adjusting num helpers (%d) to < num targets (%d), new: %d",
			numHelpers, numTargets, numTargets-1)
		numHelpers = numTargets - 1
	}

	return &threadCounts{
		nrTgts:  numTargets,
		nrHlprs: numHelpers,
	}, nil
}

// Generate a server config file from the constituent hardware components. Enforce consistent
// target and helper count across engine configs, calculate the minimum number of hugepages
// necessary for optimum performance and populate config parameters. Set NUMA affinity on the
// generated config and then run through validation.
func genServerConfig(log logging.Logger, accessPoints []string, ecs []*engine.Config, hugePageSizeKb int, tc *threadCounts) (*config.Server, error) {
	log.Debugf("setting %d targets and %d helper threads per engine", tc.nrTgts, tc.nrHlprs)
	var totNumTargets int
	for _, ec := range ecs {
		ec.WithTargetCount(tc.nrTgts).WithHelperStreamCount(tc.nrHlprs)
		totNumTargets += tc.nrTgts
	}

	reqHugePages, err := common.CalcMinHugePages(hugePageSizeKb, totNumTargets)
	if err != nil {
		return nil, errors.Wrap(err, "unable to calculate minimum hugepages")
	}

	cfg := config.DefaultServer().
		WithAccessPoints(accessPoints...).
		WithFabricProvider(ecs[0].Fabric.Provider).
		WithEngines(ecs...).
		WithControlLogFile(defaultControlLogFile).
		WithNrHugePages(reqHugePages)

	if err := cfg.SetEngineAffinities(log); err != nil {
		return nil, errors.Wrap(err, "setting engine affinities")
	}

	if err := cfg.Validate(log, hugePageSizeKb); err != nil {
		return nil, errors.Wrap(err, "validating engine config")
	}

	return cfg, nil
}
