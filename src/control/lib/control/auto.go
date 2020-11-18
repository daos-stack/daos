//
// (C) Copyright 2020 Intel Corporation.
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

	errNoNuma           = "no numa nodes reported on hosts %s"
	errUnsupNetDevClass = "unsupported net dev class in request: %s"
	errInsufNumIfaces   = "insufficient matching %s network interfaces, want %d got %d %+v"
	errInsufNumPmem     = "insufficient number of pmem devices, want %d got %d"
	errInvalNumPmem     = "unexpected number of pmem devices, want %d got %d"
	errInvalPmemNuma    = "pmem devices %v bound to unexpected numa nodes, want %v got %v"
	errInsufNumNvme     = "insufficient number of nvme devices for numa %d, want %d got %d"
	errInvalNumCores    = "invalid number of cores for numa %d"
	errInsufNumCores    = "insufficient cores for %d ssds, want %d got %d"
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

// ConfigGenerate attempts to automatically detect hardware and generate a DAOS
// server config file for a set of hosts.
func ConfigGenerate(ctx context.Context, req *ConfigGenerateReq) (*ConfigGenerateResp, error) {
	if req == nil {
		return nil, errors.Errorf("nil %T request", req)
	}
	req.Log.Debugf("ConfigGenerate called with request %v", req)

	resp := new(ConfigGenerateResp)
	networkSet, err := req.getSingleNetworkSet(ctx, resp)
	if err != nil {
		return resp, err
	}

	if err := req.checkNetwork(networkSet, resp); err != nil {
		return resp, err
	}

	if err := req.checkStorage(ctx, resp); err != nil {
		return resp, err
	}

	if err := req.checkCPUs(int(networkSet.HostFabric.CoresPerNuma), resp); err != nil {
		return resp, err
	}

	// TODO: change Validate() to take io.Writer
	//	if err := cfg.Validate(&req.buf); err != nil {
	//		return &ConfigGenerateResp{
	//			Err: errors.Wrap(err, "validation failed on auto generated config"),
	//		}, nil
	//	}

	return resp, nil
}

// getSingleNetworkSet retrieves the result of network scan over host list and
// verifies that there is only a single network set in response which indicates
// that network hardware setup is homogeneous across all hosts.
//
// Return network for a single host set or error.
func (req *ConfigGenerateReq) getSingleNetworkSet(ctx context.Context, resp *ConfigGenerateResp) (*HostFabricSet, error) {
	scanReq := new(NetworkScanReq)
	scanReq.SetHostList(req.HostList)

	scanResp, err := NetworkScan(ctx, req.Client, scanReq)
	if err != nil {
		return nil, err
	}

	if len(scanResp.GetHostErrors()) > 0 {
		resp.HostErrorsResp = scanResp.HostErrorsResp

		return nil, scanResp.Errors()
	}

	// verify homogeneous network
	numSets := len(scanResp.HostFabrics)
	switch {
	case numSets == 0:
		return nil, errors.New("no host responses")
	case numSets > 1:
		// more than one means non-homogeneous hardware
		req.Log.Info("Heterogeneous network hardware configurations detected, " +
			"cannot proceed. The following sets of hosts have different " +
			"network hardware:")
		for _, hns := range scanResp.HostFabrics {
			req.Log.Info(hns.HostSet.String())
		}

		return nil, errors.New("network hardware not consistent across hosts")
	}

	return scanResp.HostFabrics[scanResp.HostFabrics.Keys()[0]], nil
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

// parseInterfaces adds interface in scan result if class is ether or infiniband and
// the requested class is any or matches the interface class.
func (req *ConfigGenerateReq) parseInterfaces(interfaces []*HostFabricInterface) ([]*HostFabricInterface, bool) {
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
			switch req.NetClass {
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

		matching = buckets[iface.Provider].add(req.Log, iface)
		if len(matching) == req.NumPmem {
			complete = true
			break
		}
	}

	return matching, complete
}

func (req *ConfigGenerateReq) genConfig(interfaces []*HostFabricInterface) *config.Server {
	cfg := config.DefaultServer()
	for idx, iface := range interfaces {
		nn := uint(iface.NumaNode)
		iocfg := ioserver.NewConfig().
			WithTargetCount(defaultTargetCount).
			WithLogFile(fmt.Sprintf("%s.%d.log", defaultIOSrvLogFile, idx))

		iocfg.Fabric = ioserver.FabricConfig{
			Provider:       iface.Provider,
			Interface:      iface.Device,
			InterfacePort:  int(defaultFiPort + (nn * defaultFiPortInterval)),
			PinnedNumaNode: &nn,
		}

		cfg.Servers = append(cfg.Servers, iocfg)
	}

	if len(req.AccessPoints) != 0 {
		cfg = cfg.WithAccessPoints(req.AccessPoints...)
	}

	// apply global config parameters across iosrvs
	return cfg.WithSystemName(cfg.SystemName).
		WithSocketDir(cfg.SocketDir).
		WithFabricProvider(cfg.Servers[0].Fabric.Provider).
		WithSystemName(cfg.SystemName).
		WithSocketDir(cfg.SocketDir).
		WithControlLogFile(defaultControlLogFile)
}

// checkNetwork scans fabric network interfaces and updates-in-place configuration
// with appropriate fabric device details for all required numa nodes.
//
// Updates response config field in-place and returns number of cores per NUMA
// node, which is the same on all hosts in the host set.
func (req *ConfigGenerateReq) checkNetwork(hfs *HostFabricSet, resp *ConfigGenerateResp) error {
	switch req.NetClass {
	case NetDevAny, nd.Ether, nd.Infiniband:
	default:
		return errors.Errorf(errUnsupNetDevClass, nd.DevClassName(req.NetClass))
	}
	req.Log.Debugf("checkNetwork called with request %v", req)

	fabricInfo := hfs.HostFabric
	hostSet := hfs.HostSet

	if req.NumPmem == 0 {
		// the number of network interfaces and pmem namespaces should
		// be equal to the number of NUMA nodes for optimal
		if hfs.HostFabric.NumaCount == 0 {
			return errors.Errorf(errNoNuma, hostSet)
		}

		req.NumPmem = int(fabricInfo.NumaCount)
		req.Log.Debugf("minimum pmem/hfi devices required set to numa count %d",
			req.NumPmem)
	}

	// TODO: allow skipping of network validation
	// if req.SkipNetwork {
	//   return nil, nil
	// }

	msg := fmt.Sprintf(
		"Network hardware is consistent for hosts %s with %d NUMA nodes and %d cores per node:",
		hostSet.String(), fabricInfo.NumaCount, fabricInfo.CoresPerNuma)
	for _, iface := range fabricInfo.Interfaces {
		msg = fmt.Sprintf("%s\n\t%+v", msg, iface)
	}
	req.Log.Debugf(msg)

	matching, complete := req.parseInterfaces(fabricInfo.Interfaces)
	if !complete {
		class := "best-available"
		if req.NetClass != NetDevAny {
			class = nd.DevClassName(req.NetClass)
		}
		return errors.Errorf(errInsufNumIfaces, class, req.NumPmem,
			len(matching), matching)
	}

	req.Log.Debugf("selected network interfaces: %v", matching)

	resp.ConfigOut = req.genConfig(matching)

	return nil

}

// validateScmStorage verifies adequate number of pmem namespaces.
//
// Return slice of pmem block device paths with index in slice equal to NUMA
// node ID.
func (req *ConfigGenerateReq) validateScmStorage(scmNamespaces storage.ScmNamespaces) ([]string, error) {
	// sort namespaces so we can assume list index to be numa id
	sort.Slice(scmNamespaces, func(i, j int) bool {
		return scmNamespaces[i].NumaNode < scmNamespaces[j].NumaNode
	})

	pmemPaths := make([]string, 0, req.NumPmem)
	for idx, ns := range scmNamespaces {
		pmemPaths = append(pmemPaths, fmt.Sprintf("%s/%s", pmemBdevDir, ns.BlockDevice))
		if idx == req.NumPmem-1 {
			break
		}
	}
	req.Log.Debugf("selected pmem devs %v (%d)", pmemPaths, len(pmemPaths))

	if len(pmemPaths) < req.NumPmem {
		return nil, errors.Errorf(errInsufNumPmem, req.NumPmem, len(pmemPaths))
	}

	// sanity check that each pmem aligns with expected numa node
	var wantNodes, gotNodes []uint32
	for idx := range pmemPaths {
		ns := scmNamespaces[idx]
		wantNodes = append(wantNodes, uint32(idx))
		gotNodes = append(gotNodes, ns.NumaNode)

		if int(ns.NumaNode) != idx {
			return nil, errors.Errorf(errInvalPmemNuma,
				pmemPaths, wantNodes, gotNodes)
		}
	}

	return pmemPaths, nil
}

// validateNvmeStorage verifies adequate number of ctrlrs per numa node.
//
// Return slice of slices of NVMe SSD PCI addresses. All SSD addresses in group
// will be bound to NUMA node ID specified by the index of the outer slice.
//
// If zero NVMe SSDs requested (req.NumNvme) then return empty bdev slices.
func (req *ConfigGenerateReq) validateNvmeStorage(ctrlrs storage.NvmeControllers, numaCount int) ([][]string, error) {
	pciAddrsPerNuma := make([][]string, numaCount)

	if req.NumNvme == 0 {
		return pciAddrsPerNuma, nil
	}

	for _, ctrlr := range ctrlrs {
		if int(ctrlr.SocketID) > (numaCount - 1) {
			req.Log.Debugf(
				"skipping nvme device %s with numa %d (currently using %d numa nodes)",
				ctrlr.PciAddr, ctrlr.SocketID, numaCount)
			continue
		}
		pciAddrsPerNuma[ctrlr.SocketID] = append(pciAddrsPerNuma[ctrlr.SocketID],
			ctrlr.PciAddr)
	}

	for idx, numaCtrlrs := range pciAddrsPerNuma {
		num := len(numaCtrlrs)
		req.Log.Debugf("nvme pci bound to numa %d: %v (%d)", idx, numaCtrlrs, num)

		if num < req.NumNvme {
			return nil, errors.Errorf(errInsufNumNvme, idx, req.NumNvme, num)
		}
	}

	return pciAddrsPerNuma, nil
}

// getSingleStorageSet retrieves the result of storage scan over host list and
// verifies that there is only a single storage set in response which indicates
// that storage hardware setup is homogeneous across all hosts.
//
// Return storage for a single host set or error.
//
// Filter NVMe storage scan so only NUMA affinity and PCI address is taking into
// account by supplying NvmeBasic flag in scan request. This enables
// configuration to work with different models of SSDs.
func (req *ConfigGenerateReq) getSingleStorageSet(ctx context.Context, resp *ConfigGenerateResp) (*HostStorageSet, error) {
	scanReq := &StorageScanReq{NvmeBasic: true}
	scanReq.SetHostList(req.HostList)

	scanResp, err := StorageScan(ctx, req.Client, scanReq)
	if err != nil {
		return nil, err
	}
	if len(scanResp.GetHostErrors()) > 0 {
		resp.HostErrorsResp = scanResp.HostErrorsResp

		return nil, scanResp.Errors()
	}

	// verify homogeneous storage
	numSets := len(scanResp.HostStorage)
	switch {
	case numSets == 0:
		return nil, errors.New("no host responses")
	case numSets > 1:
		// more than one means non-homogeneous hardware
		req.Log.Info("Heterogeneous storage hardware configurations detected, " +
			"cannot proceed. The following sets of hosts have different " +
			"storage hardware:")
		for _, hss := range scanResp.HostStorage {
			req.Log.Info(hss.HostSet.String())
		}

		return nil, errors.New("storage hardware not consistent across hosts")
	}

	return scanResp.HostStorage[scanResp.HostStorage.Keys()[0]], nil
}

// checkStorage validates minimum NVMe and SCM device counts and populates
// ioserver storage config with detected device identifiers if thresholds met.
func (req *ConfigGenerateReq) checkStorage(ctx context.Context, resp *ConfigGenerateResp) error {
	req.Log.Debugf("checkStorage called with request %v", req)

	storageSet, err := req.getSingleStorageSet(ctx, resp)
	if err != nil {
		return err
	}

	scmNamespaces := storageSet.HostStorage.ScmNamespaces
	nvmeControllers := storageSet.HostStorage.NvmeDevices

	req.Log.Debugf(
		"Storage hardware configuration is consistent for hosts %s:\n\t%s\n\t%s",
		storageSet.HostSet.String(), scmNamespaces.Summary(),
		nvmeControllers.Summary())

	// the pmemPaths is a slice of pmem block devices each pinned to NUMA
	// node ID matching the index in the slice
	pmemPaths, err := req.validateScmStorage(scmNamespaces)
	if err != nil {
		return errors.WithMessage(err, "validating scm storage requirements")
	}

	if len(pmemPaths) != len(resp.ConfigOut.Servers) {
		return errors.Errorf(errInvalNumPmem,
			len(pmemPaths), len(resp.ConfigOut.Servers))
	}

	// bdevLists is a slice of slices of pci addresses for nvme ssd devices
	// pinned to NUMA node ID matching the index in the outer slice
	bdevLists, err := req.validateNvmeStorage(nvmeControllers, len(pmemPaths))
	if err != nil {
		return errors.WithMessage(err, "validating nvme storage requirements")
	}

	for idx, pmemPath := range pmemPaths {
		resp.ConfigOut.Servers[idx] = resp.ConfigOut.Servers[idx].
			WithScmClass(storage.ScmClassDCPM.String()).
			WithBdevClass(storage.BdevClassNvme.String()).
			WithScmMountPoint(fmt.Sprintf("%s%d", scmMountPrefix, idx)).
			WithScmDeviceList(pmemPath).
			WithBdevDeviceList(bdevLists[idx]...)
	}

	return nil
}

// checkCpus validates VOS target count and xstream helper thread count
// recommended values and populates ioserver storage config.
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
func (req *ConfigGenerateReq) checkCPUs(coresPerNuma int, resp *ConfigGenerateResp) error {
	if coresPerNuma < 1 {
		return errors.Errorf(errInvalNumCores, coresPerNuma)
	}

	for idx, ioCfg := range resp.ConfigOut.Servers {
		var numTargets int
		ssdCount := len(ioCfg.Storage.Bdev.DeviceList)
		switch ssdCount {
		case 0:
			numTargets = 16 // pmem-only mode
		case 1, 2, 4, 8:
			numTargets = 16
		case 3, 6:
			numTargets = 12
		case 5:
			numTargets = 15
		case 7:
			numTargets = 14
		case 9:
			if coresPerNuma > 18 {
				numTargets = 18
				break
			}
			numTargets = 9
		case 10:
			if coresPerNuma > 20 {
				numTargets = 20
				break
			}
			numTargets = 10
		default:
			numTargets = ssdCount
		}

		coresNeeded := numTargets + 1

		if coresPerNuma < coresNeeded {
			return errors.Errorf(errInsufNumCores,
				ssdCount, coresNeeded, coresPerNuma)
		}

		req.Log.Debugf("%d targets assigned with %d ssds", numTargets, ssdCount)

		resp.ConfigOut.Servers[idx].TargetCount = numTargets

		numHelpers := coresPerNuma - coresNeeded
		if numHelpers > numTargets {
			req.Log.Debugf(
				"adjusting num helpers (%d) to < num targets (%d)",
				numHelpers, numTargets)
			numHelpers = numTargets - 1
		}

		resp.ConfigOut.Servers[idx].HelperStreamCount = numHelpers
	}

	return nil
}
