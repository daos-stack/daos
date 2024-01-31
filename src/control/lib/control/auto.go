//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"encoding/json"
	"fmt"
	"math"
	"math/bits"
	"sort"
	"strconv"
	"strings"

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
	minNrSSDs             = 1
	minDMABuffer          = 1024
	numaCoreUsage         = 0.8 // fraction of numa cores to use for targets

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
		// Number of engines to include in generated config.
		NrEngines int `json:"NrEngines"`
		// Force use of a specific network device class for fabric comms.
		NetClass hardware.NetDevClass `json:"-"`
		// Force use of a specific fabric provider.
		NetProvider string `json:"NetProvider"`
		// Generate a config without NVMe.
		SCMOnly bool `json:"SCMOnly"`
		// Hosts to run the management service.
		AccessPoints []string `json:"-"`
		// Ports to use for fabric comms (one needed per engine).
		FabricPorts []int `json:"-"`
		// Generate config with a tmpfs RAM-disk SCM.
		UseTmpfsSCM bool `json:"UseTmpfsSCM"`
		// Location to persist control-plane metadata, will generate MD-on-SSD config.
		ExtMetadataPath string         `json:"ExtMetadataPath"`
		Log             logging.Logger `json:"-"`
	}

	// ConfGenerateResp contains the generated server config.
	ConfGenerateResp struct {
		config.Server
	}

	// ConfGenerateRemoteReq adds connectivity related fields to base request.
	ConfGenerateRemoteReq struct {
		ConfGenerateReq
		HostList []string
		Client   UnaryInvoker
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

// UnmarshalJSON unpacks JSON message into ConfGenerateReq struct.
func (cgr *ConfGenerateReq) UnmarshalJSON(data []byte) error {
	type Alias ConfGenerateReq
	aux := &struct {
		AccessPoints string
		FabricPorts  string
		NetClass     string
		*Alias
	}{
		Alias: (*Alias)(cgr),
	}

	if err := json.Unmarshal(data, &aux); err != nil {
		return err
	}

	cgr.AccessPoints = strings.Split(aux.AccessPoints, ",")
	fabricPorts := strings.Split(aux.FabricPorts, ",")
	for _, s := range fabricPorts {
		if s == "" {
			continue
		}
		n, err := strconv.Atoi(s)
		if err != nil {
			return errors.Wrap(err, "fabric ports")
		}
		cgr.FabricPorts = append(cgr.FabricPorts, n)
	}

	switch aux.NetClass {
	case "ethernet":
		cgr.NetClass = hardware.Ether
	case "infiniband":
		cgr.NetClass = hardware.Infiniband
	default:
		return errors.Errorf("unrecognized net-class value %s", aux.NetClass)
	}

	return nil
}

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
func ConfGenerate(req ConfGenerateReq, newEngineCfg newEngineCfgFn, hf *HostFabric, hs *HostStorage) (*ConfGenerateResp, error) {
	// process host fabric scan results to retrieve network details
	nd, err := getNetworkDetails(req, hf)
	if err != nil {
		return nil, err
	}

	// process host storage scan results to retrieve storage details
	sd, err := getStorageDetails(req, nd.NumaCount, hs)
	if err != nil {
		return nil, err
	}

	// evaluate device affinities to enforce locality constraints
	nodeSet, err := filterDevicesByAffinity(req, nd, sd)
	if err != nil {
		return nil, err
	}

	// populate engine configs with storage and network devices
	ecs, err := genEngineConfigs(req, newEngineCfg, nodeSet, nd, sd)
	if err != nil {
		return nil, err
	}

	// calculate service and helper thread counts
	tc, err := getThreadCounts(req.Log, ecs[0], nd.NumaCoreCount)
	if err != nil {
		return nil, err
	}

	// populate server config using engine configs
	sc, err := genServerConfig(req, ecs, sd.MemInfo, tc)
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

	ns, err := getNetworkSet(ctx, req)
	if err != nil {
		return nil, err
	}

	ss, err := getStorageSet(ctx, req)
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
func getNetworkSet(ctx context.Context, req ConfGenerateRemoteReq) (*HostFabricSet, error) {
	req.Log.Debugf("fetching host fabric info on hosts %v", req.HostList)

	scanReq := &NetworkScanReq{
		Provider: "all", // explicitly request all providers
	}
	scanReq.SetHostList(req.HostList)

	scanResp, err := NetworkScan(ctx, req.Client, scanReq)
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
		req.Log.Info("Heterogeneous network hardware configurations detected, " +
			"cannot proceed. The following sets of hosts have different " +
			"network hardware:")
		for _, hns := range scanResp.HostFabrics {
			req.Log.Info(hns.HostSet.String())
		}

		return nil, errors.New("network hardware not consistent across hosts")
	}

	networkSet := scanResp.HostFabrics[scanResp.HostFabrics.Keys()[0]]

	req.Log.Debugf("Network hardware is consistent for hosts %s:\n\t%v",
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

// parseInterfaces processes network devices in scan result, adding to a provider bucket when the
// network device class matches that requested (ETHER or INFINIBAND.
// Returns when all matching devices have been added to the relevant provider bucket.
func (pim providerIfaceMap) fromFabric(reqClass hardware.NetDevClass, prov string, ifaces []*HostFabricInterface) error {
	if pim == nil {
		return errors.Errorf("%T receiver is nil", pim)
	}

	// sort network interfaces by priority to get best available
	sort.Slice(ifaces, func(i, j int) bool {
		return ifaces[i].Priority < ifaces[j].Priority
	})

	for _, iface := range ifaces {
		if iface.NetDevClass == reqClass && (prov == "" || iface.Provider == prov) {
			pim.add(iface)
		}
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
func getNetworkDetails(req ConfGenerateReq, hf *HostFabric) (*networkDetails, error) {

	if hf == nil {
		return nil, errors.New("nil HostFabric")
	}

	switch req.NetClass {
	case hardware.Ether, hardware.Infiniband:
	default:
		return nil, errors.Errorf(errUnsupNetDevClass, req.NetClass.String())
	}

	provIfaces := make(providerIfaceMap)
	if err := provIfaces.fromFabric(req.NetClass, req.NetProvider, hf.Interfaces); err != nil {
		return nil, err
	}
	req.Log.Debugf("numa nodes: %d, numa core count: %d, available interfaces %v", hf.NumaCount,
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
func getStorageSet(ctx context.Context, req ConfGenerateRemoteReq) (*HostStorageSet, error) {
	req.Log.Debugf("fetching host storage info on hosts %v", req.HostList)

	scanReq := &StorageScanReq{NvmeBasic: true}
	scanReq.SetHostList(req.HostList)

	scanResp, err := StorageScan(ctx, req.Client, scanReq)
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
		req.Log.Info("Heterogeneous storage hardware configurations detected, " +
			"cannot proceed. The following sets of hosts have different " +
			"storage hardware:")
		for _, hss := range scanResp.HostStorage {
			req.Log.Info(hss.HostSet.String())
		}

		return nil, errors.New("storage hardware not consistent across hosts")
	}

	storageSet := scanResp.HostStorage[scanResp.HostStorage.Keys()[0]]
	hostStorage := storageSet.HostStorage

	req.Log.Debugf("Storage hardware is consistent for hosts %s:\n\t%s\n\t%s\n\t%s",
		storageSet.HostSet.String(), hostStorage.ScmNamespaces.Summary(),
		hostStorage.NvmeDevices.Summary(), hostStorage.MemInfo.Summary())

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
	NumaSCMs numaSCMsMap
	NumaSSDs numaSSDsMap
	MemInfo  *common.MemInfo
	scmCls   storage.Class
}

// getStorageDetails retrieves mappings of NUMA node to PMem and NVMe SSD devices.  Returns storage
// details struct or host error response and outer error.
func getStorageDetails(req ConfGenerateReq, numaCount int, hs *HostStorage) (*storageDetails, error) {
	if hs == nil {
		return nil, errors.New("nil HostStorage")
	}
	if hs.MemInfo == nil {
		return nil, errors.New("nil HostStorage.MemInfo")
	}

	sd := storageDetails{
		NumaSCMs: make(numaSCMsMap),
		NumaSSDs: make(numaSSDsMap),
		MemInfo: &common.MemInfo{
			HugepageSizeKiB: hs.MemInfo.HugepageSizeKiB,
			MemTotalKiB:     hs.MemInfo.MemTotalKiB,
		},
		scmCls: storage.ClassDcpm,
	}
	if sd.MemInfo.HugepageSizeKiB == 0 {
		return nil, errors.New("requires nonzero HugepageSizeKiB")
	}

	if err := sd.NumaSSDs.fromNVMe(hs.NvmeDevices); err != nil {
		return nil, errors.Wrap(err, "mapping ssd addresses to numa node")
	}

	// if tmpfs scm mode is requested, init scm map to init entry for each numa node
	if req.UseTmpfsSCM {
		if numaCount <= 0 {
			return nil, errors.New("requires nonzero numaCount")
		}
		if sd.MemInfo.MemTotalKiB == 0 {
			return nil, errors.New("requires nonzero MemTotalKiB")
		}

		req.Log.Debugf("using tmpfs for scm, one for each numa node [0-%d]", numaCount-1)
		for i := 0; i < numaCount; i++ {
			sd.NumaSCMs[i] = sort.StringSlice{""}
		}
		sd.scmCls = storage.ClassRam

		return &sd, nil
	}

	if err := sd.NumaSCMs.fromSCM(hs.ScmNamespaces); err != nil {
		return nil, errors.Wrap(err, "mapping scm block device names to numa node")
	}

	return &sd, nil
}

// Filters PMem and SSD groups to include only the NUMA IDs that have sufficient number of devices
// with appropriate affinity. Returns error if not enough satisfied NUMA ID groupings for required
// engine count.
func checkNvmeAffinity(req ConfGenerateReq, sd *storageDetails) error {
	req.Log.Debugf("numa to pmem mappings: %v", sd.NumaSCMs)
	req.Log.Debugf("numa to nvme mappings: %v", sd.NumaSSDs)

	if len(sd.NumaSCMs) < req.NrEngines {
		return errors.Errorf(errInsufNrPMemGroups, sd.NumaSCMs, req.NrEngines,
			len(sd.NumaSCMs))
	}

	if req.SCMOnly {
		req.Log.Debug("nvme disabled, skip validation")

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
		req.Log.Debugf("ssd requirements met for numa ids %v but not for %v", pass, fail)
	case len(pass) > 0:
		req.Log.Debugf("ssd requirements met for numa ids %v", pass)
	default:
		req.Log.Debugf("ssd requirements not met for numa ids %v", fail)
	}

	if len(pass) < req.NrEngines {
		// fail if the number of passing numa id groups is less than required
		req.Log.Errorf("ssd-to-numa mapping validation failed, not enough numaID groupings "+
			"satisfy SSD requirements (%d per-engine) to meet the number of required "+
			"engines (%d)", minNrSSDs, req.NrEngines)

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
	req.Log.Debugf("storage validation passed, scm: %+v, nvme: %+v", sd.NumaSCMs,
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
func chooseEngineAffinity(req ConfGenerateReq, nd *networkDetails, sd *storageDetails) ([]int, error) {
	nodes := sd.NumaSCMs.keys()

	// retrieve numa node combinations that meet storage requirements for an engine config
	nodeSets := combinations(nodes, req.NrEngines)
	if len(nodeSets) == 0 {
		return nil, errors.New("no numa node sets found in scm map")
	}
	req.Log.Debugf("numasets for possible engine configs: %v (from superset %v)", nodeSets,
		nodes)

	fabricScores, err := getFabricScores(req.Log, nodeSets, nd)
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
	req.Log.Debugf("fabric provider %s chosen on numa nodes %v", bestFabric.provider,
		bestNumaSet)

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

	// sanity check number of satisfied numa id groups matches number of engines required
	if len(nd.NumaIfaces) != req.NrEngines {
		return nil, errors.Errorf(errInsufNrIfaces, req.NrEngines, len(nd.NumaIfaces),
			nd.NumaIfaces)
	}
	if len(sd.NumaSCMs) != req.NrEngines {
		return nil, errors.Errorf(errInsufNrPMemGroups, sd.NumaSCMs, req.NrEngines,
			len(sd.NumaSCMs))
	}
	if len(sd.NumaSSDs) != req.NrEngines {
		return nil, errors.Errorf(errInsufNrSSDGroups, sd.NumaSSDs, req.NrEngines,
			len(sd.NumaSSDs))
	}

	sort.Ints(bestNumaSet)
	return bestNumaSet, nil
}

func filterDevicesByAffinity(req ConfGenerateReq, nd *networkDetails, sd *storageDetails) ([]int, error) {
	// if unset, assign number of engines based on number of NUMA nodes
	if req.NrEngines == 0 {
		req.NrEngines = nd.NumaCount
	}
	if req.NrEngines == 0 {
		return nil, errNoNuma
	}

	req.Log.Debugf("attempting to generate config with %d engines", req.NrEngines)

	if err := checkNvmeAffinity(req, sd); err != nil {
		return nil, err
	}

	nodeSet, err := chooseEngineAffinity(req, nd, sd)
	if err != nil {
		return nil, err
	}

	if len(nodeSet) == 0 {
		return nil, errors.New("generated config should have at least one engine")
	}

	return nodeSet, nil
}

func correctSSDCounts(log logging.Logger, sd *storageDetails) error {
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
			newAddrSet, err := ssds.BackingToVMDAddresses()
			if err != nil {
				return errors.Wrap(err, "converting backing addresses to vmd")
			}
			sd.NumaSSDs[numaID] = newAddrSet
		}
	}

	return nil
}

func getSCMTier(log logging.Logger, numaID, nrNumaNodes int, sd *storageDetails) (*storage.TierConfig, error) {
	scmTier := storage.NewTierConfig().WithStorageClass(sd.scmCls.String()).
		WithScmMountPoint(fmt.Sprintf("%s%d", scmMountPrefix, numaID))

	switch sd.scmCls {
	case storage.ClassRam:
	case storage.ClassDcpm:
		// Assumes only one entry per NUMA node in map.
		scmTier.WithScmDeviceList(sd.NumaSCMs[numaID][0])
	default:
		return nil, errors.Errorf("unrecognized scm tier class %q", sd.scmCls)
	}

	return scmTier, nil
}

func getBdevTiers(log logging.Logger, mdOnSSD bool, ssds *hardware.PCIAddressSet) (storage.TierConfigs, error) {
	nrSSDs := ssds.Len()
	if nrSSDs == 0 {
		log.Debugf("skip assigning ssd tiers as no ssds are available")
		return nil, nil
	}

	if !mdOnSSD {
		return storage.TierConfigs{
			storage.NewTierConfig().
				WithStorageClass(storage.ClassNvme.String()).
				WithBdevDeviceList(ssds.Strings()...),
		}, nil
	}

	// Assign SSDs to multiple tiers for MD-on-SSD, NVMe SSDs on same NUMA as
	// engine to be split into bdev tiers as follows:
	// 1 SSD: tiers 1
	// 2-5 SSDs: tiers 1:N-1
	// 6+ SSDs: tiers 2:N-2
	//
	// Bdev tier device roles are assigned later based on tier structure applied here.
	//
	// TODO: Decide whether engine config target count should be calculated based on
	//       the number of cores and number of SSDs in the second (meta+data) tier.

	var ts []int
	switch nrSSDs {
	case 1:
		ts = []int{1}
	case 2, 3, 4, 5:
		ts = []int{1, nrSSDs - 1}
	default:
		ts = []int{2, nrSSDs - 2}
	}

	log.Debugf("md-on-ssd: nr ssds per bdev tier %v", ts)

	var tiers storage.TierConfigs
	last := 0
	for _, count := range ts {
		tiers = append(tiers, storage.NewTierConfig().
			WithStorageClass(storage.ClassNvme.String()).
			WithBdevDeviceList(ssds.Strings()[last:last+count]...))
		last += count
	}

	return tiers, nil
}

type newEngineCfgFn func(int) *engine.Config

func genEngineConfigs(req ConfGenerateReq, newEngineCfg newEngineCfgFn, nodeSet []int, nd *networkDetails, sd *storageDetails) ([]*engine.Config, error) {
	nrFabPorts := len(req.FabricPorts)
	if nrFabPorts > 0 && nrFabPorts < len(nodeSet) {
		return nil, errors.Errorf("insufficient fabric ports for nr engines, want %d got %d",
			len(nodeSet), nrFabPorts)
	}

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

	// if nvme is enabled, make ssd counts consistent across numa groupings
	if !req.SCMOnly {
		if err := correctSSDCounts(req.Log, sd); err != nil {
			return nil, err
		}
	}

	cfgs := make([]*engine.Config, 0, len(nodeSet))

	req.Log.Debugf("calculating storage tiers for engines based on scm class %q", sd.scmCls)

	mdOnSSD := req.ExtMetadataPath != ""
	if mdOnSSD && sd.scmCls != storage.ClassRam {
		return nil, errors.New("md-on-ssd mode is only supported with scm class ram")
	}

	for idx, numaID := range nodeSet {
		ssds := sd.NumaSSDs[numaID]
		iface := nd.NumaIfaces[numaID]

		scmTier, err := getSCMTier(req.Log, numaID, len(nodeSet), sd)
		if err != nil {
			return nil, err
		}
		tiers := storage.TierConfigs{scmTier}

		bdevTiers, err := getBdevTiers(req.Log, mdOnSSD, ssds)
		if err != nil {
			return nil, errors.Wrapf(err, "calculating bdev tiers")
		}
		tiers = append(tiers, bdevTiers...)

		cfg := newEngineCfg(len(cfgs)).WithStorage(tiers...)

		ifPort := int(defaultFiPort + (idx * defaultFiPortInterval))
		if nrFabPorts > 0 {
			ifPort = req.FabricPorts[idx]
		}

		pnn := uint(numaID)
		cfg.PinnedNumaNode = &pnn
		cfg.Fabric = engine.FabricConfig{
			Provider:      iface.Provider,
			Interface:     iface.Device,
			InterfacePort: ifPort,
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
// counts. The following algorithm is implemented after validating against edge cases:
//
// targets_per_ssd = ROUNDDOWN(#cores_per_engine * 0.8 / #ssds_per_engine; 0)
// targets_per_engine = #ssds_per_engine * #targets_per_ssd
// xs_streams_per_engine = ROUNDDOWN(#targets_per_engine / 4; 0)
//
// Here, 0.8 = 4/5 = #targets / (#targets + #xs_streams).
func getThreadCounts(log logging.Logger, ec *engine.Config, coresPerEngine int) (*threadCounts, error) {
	if ec == nil {
		return nil, errors.Errorf("nil %T parameter", ec)
	}
	if coresPerEngine < 2 {
		return nil, errors.Errorf(errInvalNrCores, coresPerEngine)
	}

	ssdsPerEngine := ec.Storage.Tiers.NVMeBdevs().Len()

	// first handle atypical edge cases
	if ssdsPerEngine == 0 {
		tgtsPerEngine := defaultTargetCount
		if tgtsPerEngine >= coresPerEngine {
			tgtsPerEngine = coresPerEngine - 1
		}
		log.Debugf("nvme disabled, %d targets assigned and 0 helper threads", tgtsPerEngine)

		return &threadCounts{
			nrTgts:  tgtsPerEngine,
			nrHlprs: 0,
		}, nil
	}

	// TODO DAOS-11859: Calculate based on data role SSDs only.
	tgtsPerSSD := int((float64(coresPerEngine) * numaCoreUsage) / float64(ssdsPerEngine))
	tgtsPerEngine := ssdsPerEngine * tgtsPerSSD

	if tgtsPerSSD == 0 {
		if ssdsPerEngine > (coresPerEngine - 1) {
			tgtsPerEngine = coresPerEngine - 1
		} else {
			tgtsPerEngine = ssdsPerEngine
		}

		log.Debugf("80-percent-of-cores:ssd ratio is less than 1 (%.2f:%.2f), use %d tgts",
			float64(coresPerEngine)*numaCoreUsage, float64(ssdsPerEngine),
			tgtsPerEngine)

		return &threadCounts{
			nrTgts:  tgtsPerEngine,
			nrHlprs: 0,
		}, nil
	}

	tc := threadCounts{
		nrTgts:  tgtsPerEngine,
		nrHlprs: tgtsPerEngine / 4,
	}

	log.Debugf("per-engine %d targets assigned with %d ssds (based on %d cores available), "+
		"%d helper xstreams", tc.nrTgts, ssdsPerEngine, coresPerEngine, tc.nrHlprs)

	return &tc, nil
}

// check that all access points either have no port specified or have the same port number.
func checkAccessPointPorts(log logging.Logger, aps []string) (int, error) {
	if len(aps) == 0 {
		return 0, errors.New("no access points")
	}

	port := -1
	for _, ap := range aps {
		apPort, err := config.GetAccessPointPort(log, ap)
		if err != nil {
			return 0, errors.Wrapf(err, "access point %q", ap)
		}
		if port == -1 {
			port = apPort
			continue
		}
		if apPort != port {
			return 0, errors.New("access point port numbers do not match")
		}
	}

	return port, nil
}

// Generate a server config file from the constituent hardware components. Enforce consistent
// target and helper count across engine configs necessary for optimum performance and populate
// config parameters. Set NUMA affinity on the generated config and then run through validation.
func genServerConfig(req ConfGenerateReq, ecs []*engine.Config, mi *common.MemInfo, tc *threadCounts) (*config.Server, error) {
	if len(ecs) == 0 {
		return nil, errors.New("expected non-zero number of engine configs")
	}

	req.Log.Debugf("setting %d targets and %d helper threads per engine", tc.nrTgts, tc.nrHlprs)
	for _, ec := range ecs {
		ec.WithTargetCount(tc.nrTgts).WithHelperStreamCount(tc.nrHlprs)
	}

	cfg := config.DefaultServer().
		WithAccessPoints(req.AccessPoints...).
		WithFabricProvider(ecs[0].Fabric.Provider).
		WithEngines(ecs...).
		WithControlLogFile(defaultControlLogFile)

	for idx := range cfg.Engines {
		tiers := cfg.Engines[idx].Storage.Tiers
		if err := tiers.AssignBdevTierRoles(req.ExtMetadataPath); err != nil {
			return nil, errors.Wrapf(err, "assigning engine %d storage bdev tier roles",
				idx)
		}
		// Add default control_metadata path if roles have been assigned.
		if idx == 0 && tiers.HasBdevRoleMeta() {
			cfg.Metadata = storage.ControlMetadata{
				Path: req.ExtMetadataPath,
			}
		}
	}

	portNum, err := checkAccessPointPorts(req.Log, cfg.AccessPoints)
	if err != nil {
		return nil, err
	}
	if portNum != 0 {
		// Custom access point port number specified so set server port to the same.
		cfg.WithControlPort(portNum)
	}

	if err := cfg.Validate(req.Log); err != nil {
		return nil, errors.Wrap(err, "validating engine config")
	}

	if err := cfg.SetNrHugepages(req.Log, mi); err != nil {
		return nil, err
	}

	if err := cfg.SetRamdiskSize(req.Log, mi); err != nil {
		return nil, err
	}

	return cfg, nil
}
