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
	"sort"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	scmMountPrefix = "/mnt/daos"
	pmemBdevDir    = "/dev"
)

type (
	// ConfigGenerateReq contains the inputs for the request.
	ConfigGenerateReq struct {
		unaryRequest
		msRequest
		NumPmem  int
		NumNvme  int
		NetClass string
		Client   UnaryInvoker
		HostList []string
		Log      logging.Logger
	}

	// ConfigGenerateResp contains the request response.
	ConfigGenerateResp struct {
		ConfigOut *config.Server
		Err       error
	}

	numaNumGetter func() (int, error)
)

func getNumaCount() (int, error) {
	netCtx, err := netdetect.Init(context.Background())
	if err != nil {
		return 0, err
	}
	defer netdetect.CleanUp(netCtx)

	return netdetect.NumNumaNodes(netCtx), nil
}

// ConfigGenerate attempts to automatically detect hardware and generate a DAOS
// server config file for a set of hosts.
func ConfigGenerate(ctx context.Context, req *ConfigGenerateReq) (*ConfigGenerateResp, error) {
	if req == nil {
		return nil, errors.Errorf("nil %T request", req)
	}
	req.Log.Debugf("ConfigGenerate called with request %v", req)

	cfg, err := req.checkStorage(ctx, getNumaCount)
	if err != nil {
		return &ConfigGenerateResp{Err: err}, nil
	}

	cfg, err = req.checkNetwork(ctx, cfg)
	if err != nil {
		return &ConfigGenerateResp{Err: err}, nil
	}

	// TODO: change Validate() to take io.Writer
	//	if err := cfg.Validate(&req.buf); err != nil {
	//		return &ConfigGenerateResp{
	//			Err: errors.Wrap(err, "validation failed on auto generated config"),
	//		}, nil
	//	}

	return &ConfigGenerateResp{ConfigOut: cfg}, nil
}

// validateScmStorage verifies adequate number of pmem namespaces.
//
// Return slice of pmem block device paths with index in slice equal to NUMA
// node ID.
func (req *ConfigGenerateReq) validateScmStorage(scmNamespaces storage.ScmNamespaces, getNumNuma numaNumGetter) ([]string, error) {
	minPmems := req.NumPmem
	if minPmems == 0 {
		// detect number of NUMA nodes as by default this should be
		// the number of pmem namespaces for optimal config
		numaCount, err := getNumNuma()
		if err != nil {
			return nil, errors.Wrap(err, "retrieve number of NUMA nodes on host")
		}
		if numaCount == 0 {
			return nil, errors.New("no NUMA nodes reported on host")
		}

		minPmems = numaCount
		req.Log.Debugf("minimum pmem devices required set to numa count %d", numaCount)
	}

	// sort namespaces so we can assume list index to be numa id
	sort.Slice(scmNamespaces, func(i, j int) bool {
		return scmNamespaces[i].NumaNode < scmNamespaces[j].NumaNode
	})

	pmemPaths := make([]string, 0, len(scmNamespaces))
	for _, ns := range scmNamespaces {
		pmemPaths = append(pmemPaths, fmt.Sprintf("%s/%s", pmemBdevDir, ns.BlockDevice))
	}
	req.Log.Debugf("pmem devs %v (%d)", pmemPaths, len(pmemPaths))

	if len(scmNamespaces) < minPmems {
		return nil, errors.Errorf("insufficient number of pmem devices, want %d got %d",
			minPmems, len(scmNamespaces))
	}

	// sanity check that each pmem aligns with expected numa node
	for idx := range pmemPaths {
		if int(scmNamespaces[idx].NumaNode) != idx {
			return nil, errors.Errorf("unexpected numa node for scm %v, want %d",
				scmNamespaces[idx], idx)
		}
	}

	return pmemPaths, nil
}

// validateNvmeStorage verifies adequate number of ctrlrs per numa node.
//
// Return slice of slices of NVMe SSD PCI addresses. All SSD addresses in group
// will be bound to NUMA node ID specified by the index of the outer slice.
func (req *ConfigGenerateReq) validateNvmeStorage(ctrlrs storage.NvmeControllers, numaCount int) ([][]string, error) {
	minCtrlrs := req.NumNvme
	if minCtrlrs == 0 {
		minCtrlrs = 1 // minimum per numa node
	}

	pciAddrsPerNuma := make([][]string, numaCount)
	for _, ctrlr := range ctrlrs {
		if int(ctrlr.SocketID) > (numaCount - 1) {
			req.Log.Debugf(
				"skipping nvme device %s with numa %d (in use numa count is %d)",
				ctrlr.PciAddr, ctrlr.SocketID, numaCount)
			continue
		}
		pciAddrsPerNuma[ctrlr.SocketID] = append(pciAddrsPerNuma[ctrlr.SocketID],
			ctrlr.PciAddr)
	}

	for idx, numaCtrlrs := range pciAddrsPerNuma {
		num := len(numaCtrlrs)
		req.Log.Debugf("nvme pci bound to numa %d: %v (%d)", idx, numaCtrlrs, num)

		if num < minCtrlrs {
			return nil, errors.Errorf(
				"insufficient number of nvme devices for numa node %d, want %d got %d",
				idx, minCtrlrs, num)
		}
	}

	return pciAddrsPerNuma, nil
}

// getSingleStorageSet retrieves the result of storage scan over host list and
// verifies that there is only a single storage set in response which indicates
// that storage hardware setup is homogeneous across all hosts.
//
// Return storage for a single host set or error.
func (req *ConfigGenerateReq) getSingleStorageSet(ctx context.Context) (*HostStorageSet, error) {
	scanReq := &StorageScanReq{}
	scanReq.SetHostList(req.HostList)

	scanResp, err := StorageScan(ctx, req.Client, scanReq)
	if err != nil {
		return nil, err
	}

	if scanResp.Errors() != nil {
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
//
// Return server config populated with ioserver storage or error.
func (req *ConfigGenerateReq) checkStorage(ctx context.Context, getNumNuma numaNumGetter) (*config.Server, error) {
	req.Log.Debugf("checkStorage called with request %v", req)

	storageSet, err := req.getSingleStorageSet(ctx)
	if err != nil {
		return nil, err
	}
	scmNamespaces := storageSet.HostStorage.ScmNamespaces
	nvmeControllers := storageSet.HostStorage.NvmeDevices

	req.Log.Infof("Storage hardware configuration is consistent for hosts %s:\n\t%s\n\t%s",
		storageSet.HostSet.String(), scmNamespaces.Summary(), nvmeControllers.Summary())

	// the pmemPaths is a slice of pmem block devices each pinned to NUMA
	// node ID matching the index in the slice
	pmemPaths, err := req.validateScmStorage(scmNamespaces, getNumNuma)
	if err != nil {
		return nil, errors.WithMessage(err, "validating scm storage requirements")
	}

	// bdevLists is a slice of slices of pci addresses for nvme ssd devices
	// pinned to NUMA node ID matching the index in the outer slice
	bdevLists, err := req.validateNvmeStorage(nvmeControllers, len(pmemPaths))
	if err != nil {
		return nil, errors.WithMessage(err, "validating nvme storage requirements")
	}

	cfg := config.DefaultServer()
	for idx, pp := range pmemPaths {
		cfg.Servers = append(cfg.Servers, ioserver.NewConfig().
			WithScmClass(storage.ScmClassDCPM.String()).
			WithBdevClass(storage.BdevClassNvme.String()).
			WithScmMountPoint(fmt.Sprintf("%s%d", scmMountPrefix, idx)).
			WithScmDeviceList(pp).
			WithBdevDeviceList(bdevLists[idx]...))
	}
	cfg = cfg.WithSystemName(cfg.SystemName) // reset io cfgs to global setting

	return cfg.WithSocketDir(cfg.SocketDir), nil
}

func (req *ConfigGenerateReq) checkNetwork(ctx context.Context, cfg *config.Server) (*config.Server, error) {
	netCtx, err := netdetect.Init(ctx)
	if err != nil {
		return nil, errors.Wrap(err, "initializing netdetect library")
	}
	defer netdetect.CleanUp(netCtx)

	fabricData, err := netdetect.ScanFabric(netCtx, "")
	if err != nil {
		return nil, errors.Wrap(err, "scanning fabric info")
	}
	req.Log.Debugf("fabric scan: %v", fabricData)

	// sort fabric results in ascending priority (lowest is fastest)
	sort.Slice(fabricData, func(i, j int) bool {
		return fabricData[i].Priority < fabricData[j].Priority
	})

	for idx, fd := range fabricData {
		req.Log.Debugf("%d. %s", idx, fd)
	}

	// identify interfaces for all numa nodes
	return cfg, nil // TODO: implement
}
