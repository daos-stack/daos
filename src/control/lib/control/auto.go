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
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	scmMountPrefix        = "/mnt/daos"
	pmemBdevDir           = "/dev"
	defaultFiPort         = 31416
	defaultFiPortInterval = 1000
	defaultTargetCount    = 16
	defaultIOSrvLogFile   = "/tmp/daos_io_server"
	defaultControlLogFile = "/tmp/daos_server.log"
	// NetDevAny matches any netdetect network device class
	NetDevAny = math.MaxUint32

	errNoNuma            = "zero numa nodes reported on hosts %s"
	errUnsupNetDevClass  = "unsupported net dev class in request: %s"
	errInsufNumIfaces    = "insufficient matching %s network interfaces, want %d got %d %+v"
	errInsufNumPmem      = "insufficient number of pmem devices %v, want %d got %d"
	errInvalNumPmem      = "unexpected number of pmem devices, want %d got %d"
	errInvalNumNvme      = "unexpected number of nvme device groups, want %d got %d"
	errInvalPmemNuma     = "pmem devices %v bound to unexpected numa nodes, want %v got %v"
	errInsufNumNvme      = "insufficient number of nvme devices for numa %d, want %d got %d"
	errInvalNumCores     = "invalid number of cores for numa %d"
	errInsufNumCores     = "insufficient cores for %d ssds, want %d got %d"
	errInvalNumTgtCounts = "unexpected number of target count values, want %d got %d"
)

type (
	// ConfigGenerateReq contains the inputs for the request.
	ConfigGenerateReq struct {
		unaryRequest
		msRequest
		NumPmem      int
		NumNvme      int
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

// getNetworkParams retrieves recommended network interfaces for the chosen set
// of NUMA nodes that will populate the fabric parameters in the server config.
//
// Returns slice of fabric interfaces, host errors and outer error.
func getNetworkParams(ctx context.Context, req ConfigGenerateReq) ([]*HostFabricInterface, *HostFabricSet, *HostErrorsResp, error) {
	hostErrs, netSet, err := getSingleNetworkSet(ctx, req.Log, req.HostList, req.Client)
	if err != nil {
		return nil, nil, hostErrs, err
	}

	ifaces, err := checkNetwork(req.Log, req.NetClass, req.NumPmem, netSet)
	if err != nil {
		return nil, nil, nil, err
	}

	return ifaces, netSet, nil, nil
}

// getStorageParams retrieves recommended pmem device path and NVMe SSD PCI
// addresses used to populate scm_list and bdev_list parameters in server
// config.
//
// Returns pmem block device paths, SSD PCI address lists or host error response
// and outer error.
func getStorageParams(ctx context.Context, req ConfigGenerateReq, numNuma int) ([]string, [][]string, *HostErrorsResp, error) {
	hostErrs, storSet, err := getSingleStorageSet(ctx, req.Log, req.HostList, req.Client)
	if err != nil {
		return nil, nil, hostErrs, err
	}

	pmemPaths, bdevLists, err := checkStorage(req.Log, numNuma, req.NumNvme, storSet)
	if err != nil {
		return nil, nil, nil, err
	}

	return pmemPaths, bdevLists, nil, nil
}

// getCPUParams retrieves recommended VOS target count and helper xstream thread
// count parameters for server config.
//
// Returns target and helper thread counts and error.
func getCPUParams(log logging.Logger, bdevLists [][]string, coresPerNuma int) ([]int, []int, error) {
	if coresPerNuma < 1 {
		return nil, nil, errors.Errorf(errInvalNumCores, coresPerNuma)
	}

	ioNumTgts := make([]int, len(bdevLists))
	ioNumHlprs := make([]int, len(bdevLists))
	for idx, bdevList := range bdevLists {
		nTgts, nHlprs, err := checkCPUs(log, len(bdevList), coresPerNuma)
		if err != nil {
			return nil, nil, err
		}
		ioNumTgts[idx] = nTgts
		ioNumHlprs[idx] = nHlprs
	}

	return ioNumTgts, ioNumHlprs, nil
}

// ConfigGenerate attempts to automatically detect hardware and generate a DAOS
// server config file for a set of hosts with homogeneous hardware setup.
//
// Returns API response and error.
func ConfigGenerate(ctx context.Context, req ConfigGenerateReq) (*ConfigGenerateResp, error) {
	req.Log.Debugf("ConfigGenerate called with request %v", req)

	ifaces, netSet, hostErrs, err := getNetworkParams(ctx, req)
	if err != nil {
		if hostErrs == nil {
			hostErrs = &HostErrorsResp{}
		}

		return &ConfigGenerateResp{HostErrorsResp: *hostErrs}, err
	}

	pmemPaths, bdevLists, hostErrs, err := getStorageParams(ctx, req, len(ifaces))
	if err != nil {
		if hostErrs == nil {
			hostErrs = &HostErrorsResp{}
		}

		return &ConfigGenerateResp{HostErrorsResp: *hostErrs}, err
	}

	ioNumTgts, ioNumHlprs, err := getCPUParams(req.Log, bdevLists,
		int(netSet.HostFabric.CoresPerNuma))
	if err != nil {
		return nil, err
	}

	cfg, err := genConfig(req.AccessPoints, pmemPaths, ifaces, bdevLists,
		ioNumTgts, ioNumHlprs)
	if err != nil {
		return nil, err
	}

	// TODO: change Validate() to take io.Writer
	//	if err := cfg.Validate(&req.buf); err != nil {
	//		return &ConfigGenerateResp{
	//			Err: errors.Wrap(err, "validation failed on auto generated config"),
	//		}, nil
	//	}

	return &ConfigGenerateResp{ConfigOut: cfg}, nil
}

// getSingleNetworkSet retrieves the result of network scan over host list and
// verifies that there is only a single network set in response which indicates
// that network hardware setup is homogeneous across all hosts.
//
// Return host errors, network scan results for the host set or error.
func getSingleNetworkSet(ctx context.Context, log logging.Logger, hostList []string, client UnaryInvoker) (*HostErrorsResp, *HostFabricSet, error) {
	scanReq := new(NetworkScanReq)
	scanReq.SetHostList(hostList)

	scanResp, err := NetworkScan(ctx, client, scanReq)
	if err != nil {
		return nil, nil, err
	}

	if len(scanResp.GetHostErrors()) > 0 {
		return &scanResp.HostErrorsResp, nil, scanResp.Errors()
	}

	// verify homogeneous network
	numSets := len(scanResp.HostFabrics)
	switch {
	case numSets == 0:
		return nil, nil, errors.New("no host responses")
	case numSets > 1:
		// more than one means non-homogeneous hardware
		log.Info("Heterogeneous network hardware configurations detected, " +
			"cannot proceed. The following sets of hosts have different " +
			"network hardware:")
		for _, hns := range scanResp.HostFabrics {
			log.Info(hns.HostSet.String())
		}

		return nil, nil, errors.New("network hardware not consistent across hosts")
	}

	return nil, scanResp.HostFabrics[scanResp.HostFabrics.Keys()[0]], nil
}

type classInterfaces map[uint32][]*HostFabricInterface

// add interface to bucket corresponding to provider and network class type,
// verify NUMA node binding doesn't match existing entry in bucket.
func (cis classInterfaces) add(log logging.Logger, iface *HostFabricInterface) []*HostFabricInterface {
	for _, existing := range cis[iface.NetDevClass] {
		if existing.NumaNode == iface.NumaNode {
			// already have interface for this NUMA
			return cis[iface.NetDevClass]
		}
	}
	log.Debugf("%s class iface %s found for NUMA %d",
		nd.DevClassName(iface.NetDevClass), iface.Device, iface.NumaNode)
	cis[iface.NetDevClass] = append(cis[iface.NetDevClass], iface)

	return cis[iface.NetDevClass]
}

// parseInterfaces adds interface in scan result, added on following condition:
// IF class == (ether OR infiniband) AND requested_class == (ANY OR <class>).
func parseInterfaces(log logging.Logger, reqClass uint32, numPmem int, interfaces []*HostFabricInterface) ([]*HostFabricInterface, bool) {
	// sort network interfaces by priority to get best available
	sort.Slice(interfaces, func(i, j int) bool {
		return interfaces[i].Priority < interfaces[j].Priority
	})

	var complete bool
	var matching []*HostFabricInterface
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

		if _, exists := buckets[iface.Provider]; !exists {
			buckets[iface.Provider] = make(classInterfaces)
		}

		matching = buckets[iface.Provider].add(log, iface)
		if len(matching) == numPmem {
			complete = true
			break
		}
	}

	return matching, complete
}

// checkNetwork scans fabric network interfaces and returns a slice of
// interfaces matching the number and NUMA affinity of assigned pmem
// devices.
func checkNetwork(log logging.Logger, reqClass uint32, numPmem int, hfs *HostFabricSet) ([]*HostFabricInterface, error) {
	switch reqClass {
	case NetDevAny, nd.Ether, nd.Infiniband:
	default:
		return nil, errors.Errorf(errUnsupNetDevClass, nd.DevClassName(reqClass))
	}

	fabric := hfs.HostFabric
	hostSet := hfs.HostSet

	if numPmem == 0 {
		// the number of network interfaces and pmem namespaces should
		// be equal to the number of NUMA nodes for optimal
		if hfs.HostFabric.NumaCount == 0 {
			return nil, errors.Errorf(errNoNuma, hostSet)
		}
		numPmem = int(fabric.NumaCount)
		log.Debugf("minimum pmem/hfi devices required set to numa count %d", numPmem)
	}

	msg := fmt.Sprintf(
		"Network hardware consistent on hosts %s (%d NUMA nodes and %d cores per node):",
		hostSet.String(), fabric.NumaCount, fabric.CoresPerNuma)
	for _, iface := range fabric.Interfaces {
		msg = fmt.Sprintf("%s\n\t%+v", msg, iface)
	}
	log.Debugf(msg)

	matchIfaces, complete := parseInterfaces(log, reqClass, numPmem, fabric.Interfaces)
	if !complete {
		class := "best-available"
		if reqClass != NetDevAny {
			class = nd.DevClassName(reqClass)
		}
		return nil, errors.Errorf(errInsufNumIfaces, class, numPmem, len(matchIfaces),
			matchIfaces)
	}

	log.Debugf("selected network interfaces: %v", matchIfaces)

	return matchIfaces, nil
}

// validateScmStorage verifies adequate number of pmem namespaces.
//
// Return slice of pmem block device paths with index in slice equal to NUMA
// node ID.
func validateScmStorage(numPmem int, namespaces storage.ScmNamespaces) ([]string, error) {
	// sort namespaces so we can assume list index to be numa id
	sort.Slice(namespaces, func(i, j int) bool {
		return namespaces[i].NumaNode < namespaces[j].NumaNode
	})

	pmemPaths := make([]string, 0, numPmem)
	for idx, ns := range namespaces {
		pmemPaths = append(pmemPaths, fmt.Sprintf("%s/%s", pmemBdevDir, ns.BlockDevice))
		if idx == numPmem-1 {
			break
		}
	}
	if len(pmemPaths) < numPmem {
		return nil, errors.Errorf(errInsufNumPmem, pmemPaths, numPmem, len(pmemPaths))
	}

	// sanity check that each pmem aligns with expected numa node
	var wantNodes, gotNodes []uint32
	for idx := range pmemPaths {
		ns := namespaces[idx]
		wantNodes = append(wantNodes, uint32(idx))
		gotNodes = append(gotNodes, ns.NumaNode)

		if int(ns.NumaNode) != idx {
			return nil, errors.Errorf(errInvalPmemNuma, pmemPaths, wantNodes, gotNodes)
		}
	}

	return pmemPaths, nil
}

// validateNvmeStorage verifies adequate number of ctrlrs per numa node.
//
// Return slice of slices of NVMe SSD PCI addresses. All SSD addresses in group
// will be bound to NUMA node ID specified by the index of the outer slice.
//
// If zero NVMe SSDs requested (reqNumNvme) then return empty bdev slices.
func validateNvmeStorage(log logging.Logger, numNuma int, reqNumNvme int, ctrlrs storage.NvmeControllers) ([][]string, error) {
	pciAddrsPerNuma := make([][]string, numNuma)

	if reqNumNvme == 0 {
		return pciAddrsPerNuma, nil
	}

	for _, ctrlr := range ctrlrs {
		if int(ctrlr.SocketID) > (numNuma - 1) {
			log.Debugf("skipping nvme device %s with numa %d (currently using %d numa nodes)",
				ctrlr.PciAddr, ctrlr.SocketID, numNuma)
			continue
		}
		pciAddrsPerNuma[ctrlr.SocketID] = append(pciAddrsPerNuma[ctrlr.SocketID],
			ctrlr.PciAddr)
	}

	for idx, numaCtrlrs := range pciAddrsPerNuma {
		num := len(numaCtrlrs)
		log.Debugf("nvme pci bound to numa %d: %v (%d)", idx, numaCtrlrs, num)

		if num < reqNumNvme {
			return nil, errors.Errorf(errInsufNumNvme, idx, reqNumNvme, num)
		}
	}

	return pciAddrsPerNuma, nil
}

// getSingleStorageSet retrieves the result of storage scan over host list and
// verifies that there is only a single storage set in response which indicates
// that storage hardware setup is homogeneous across all hosts.
//
// Filter NVMe storage scan so only NUMA affinity and PCI address is taking into
// account by supplying NvmeBasic flag in scan request. This enables
// configuration to work with different models of SSDs.
//
// Return host errors, storage scan results for the host set or error.
func getSingleStorageSet(ctx context.Context, log logging.Logger, hostList []string, client UnaryInvoker) (*HostErrorsResp, *HostStorageSet, error) {
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
	numSets := len(scanResp.HostStorage)
	switch {
	case numSets == 0:
		return nil, nil, errors.New("no host responses")
	case numSets > 1:
		// more than one means non-homogeneous hardware
		log.Info("Heterogeneous storage hardware configurations detected, " +
			"cannot proceed. The following sets of hosts have different " +
			"storage hardware:")
		for _, hss := range scanResp.HostStorage {
			log.Info(hss.HostSet.String())
		}

		return nil, nil, errors.New("storage hardware not consistent across hosts")
	}

	return nil, scanResp.HostStorage[scanResp.HostStorage.Keys()[0]], nil
}

// checkStorage generates recommended device allocations for NVMe and SCM based
// on requested thresholds and NUMA bindings of available devices.
func checkStorage(log logging.Logger, numPmem, reqNumNvme int, storageSet *HostStorageSet) ([]string, [][]string, error) {
	scmNamespaces := storageSet.HostStorage.ScmNamespaces
	nvmeControllers := storageSet.HostStorage.NvmeDevices

	log.Debugf("Storage hardware configuration is consistent for hosts %s:\n\t%s\n\t%s",
		storageSet.HostSet.String(), scmNamespaces.Summary(), nvmeControllers.Summary())

	// the pmemPaths is a slice of pmem block devices each pinned to NUMA
	// node ID matching the index in the slice
	pmemPaths, err := validateScmStorage(numPmem, scmNamespaces)
	if err != nil {
		return nil, nil, errors.WithMessage(err, "validating scm storage requirements")
	}
	log.Debugf("selected pmem devs %v (%d)", pmemPaths, len(pmemPaths))

	if len(pmemPaths) != numPmem {
		return nil, nil, errors.Errorf(errInvalNumPmem, numPmem, len(pmemPaths))
	}

	// bdevLists is a slice of slices of pci addresses for nvme ssd devices
	// pinned to NUMA node ID matching the index in the outer slice
	bdevLists, err := validateNvmeStorage(log, numPmem, reqNumNvme, nvmeControllers)
	if err != nil {
		return nil, nil, errors.WithMessage(err, "validating nvme storage requirements")
	}

	if len(pmemPaths) != len(bdevLists) {
		return nil, nil, errors.Errorf(errInvalNumNvme, len(pmemPaths), len(bdevLists))
	}

	return pmemPaths, bdevLists, nil
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

// checkCPUs validates and returns VOS target count and xstream helper thread count
// recommended values
//
// The target count should be a multiplier of the number of SSDs and typically
// daos gets the best performance with 16x targets per I/O Server so target
// count will be between 12 and 20.
//
// Validate number of targets + 1 cores are available per IO servers, not
// usually a problem as sockets normally have at least 18 cores.
//
// Create helper threads for the remaining available cores, e.g. with 24 cores,
// allocate 7 helper threads. Number of helper threads should never be more than
// number of targets.
//
// TODO: generalize formula.
func checkCPUs(log logging.Logger, numSSDs, coresPerNUMA int) (int, int, error) {
	var numTargets int
	if numSSDs == 0 {
		numTargets = defaultTargetCount
		if numTargets >= coresPerNUMA {
			return coresPerNUMA - 1, 0, nil
		}

		return numTargets, calcHelpers(log, numTargets, coresPerNUMA), nil
	}

	if numSSDs >= coresPerNUMA {
		return 0, 0, errors.Errorf("need more cores than ssds, got %d want %d",
			coresPerNUMA, numSSDs)
	}

	for tgts := numSSDs; tgts < coresPerNUMA; tgts += numSSDs {
		numTargets = tgts
	}

	log.Debugf("%d targets assigned with %d ssds", numTargets, numSSDs)

	return numTargets, calcHelpers(log, numTargets, coresPerNUMA), nil
}

func defaultIOSrvCfg(idx int) *ioserver.Config {
	return ioserver.NewConfig().
		WithTargetCount(defaultTargetCount).
		WithLogFile(fmt.Sprintf("%s.%d.log", defaultIOSrvLogFile, idx)).
		WithScmClass(storage.ScmClassDCPM.String()).
		WithBdevClass(storage.BdevClassNvme.String())
}

// genConfig validates input device lists and generates server config file from
// calculated network, storage and CPU parameters.
func genConfig(accessPoints, pmemPaths []string, ifaces []*HostFabricInterface, bdevLists [][]string, nTgts, nHlprs []int) (*config.Server, error) {
	if len(pmemPaths) == 0 {
		return nil, errors.Errorf(errInvalNumPmem, 1, 0)
	}
	if len(ifaces) != len(pmemPaths) {
		return nil, errors.Errorf(errInsufNumIfaces, "", len(pmemPaths), len(ifaces),
			ifaces)
	}
	if len(nTgts) != len(ifaces) {
		return nil, errors.Errorf(errInvalNumTgtCounts, len(ifaces), len(nTgts))
	}
	if len(bdevLists) != len(pmemPaths) {
		return nil, errors.New("programming error, bdevLists != pmemPaths")
	}

	cfg := config.DefaultServer()
	for idx, iface := range ifaces {
		nn := uint(iface.NumaNode)
		iocfg := defaultIOSrvCfg(idx).
			WithScmMountPoint(fmt.Sprintf("%s%d", scmMountPrefix, idx)).
			WithScmDeviceList(pmemPaths[idx]).
			WithBdevDeviceList(bdevLists[idx]...).
			WithTargetCount(nTgts[idx]).
			WithHelperStreamCount(nHlprs[idx])

		iocfg.Fabric = ioserver.FabricConfig{
			Provider:       iface.Provider,
			Interface:      iface.Device,
			InterfacePort:  int(defaultFiPort + (nn * defaultFiPortInterval)),
			PinnedNumaNode: &nn,
		}

		cfg.Servers = append(cfg.Servers, iocfg)
	}

	if len(accessPoints) != 0 {
		cfg = cfg.WithAccessPoints(accessPoints...)
	}

	// apply global config parameters across iosrvs
	return cfg.WithSystemName(cfg.SystemName).
		WithSocketDir(cfg.SocketDir).
		WithFabricProvider(cfg.Servers[0].Fabric.Provider).
		WithSystemName(cfg.SystemName).
		WithSocketDir(cfg.SocketDir).
		WithControlLogFile(defaultControlLogFile), nil
}
