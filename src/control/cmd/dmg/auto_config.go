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

package main

import (
	"context"
	"fmt"
	"strings"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/pkg/errors"
	"gopkg.in/yaml.v2"
)

var (
	scmMountPrefix = "/mnt/daos"
	pmemBdevDir    = "/dev"
)

// configCmd is the struct representing the top-level config subcommand.
type configCmd struct {
	Generate configGenCmd `command:"generate" alias:"g" description:"Generate DAOS server configuration file based on discoverable hardware devices"`
}

type configGenCmd struct {
	logCmd
	cfgCmd
	ctlInvokerCmd
	hostListCmd
	jsonOutputCmd
	NumPmem  int    `short:"p" long:"num-pmem" description:"Minimum number of SCM (pmem) devices required per storage host in DAOS system"`
	NumNvme  int    `short:"n" long:"num-nvme" description:"Minimum number of NVMe devices required per storage host in DAOS system"`
	NetClass string `default:"best-available" short:"c" long:"net-class" description:"Network class preferred, defaults to best available" choice:"best-available" choice:"ethernet" choice:"infiniband"`
}

type numaNumGetter func() (int, error)

func getNumaCount() (int, error) {
	netCtx, err := netdetect.Init(context.Background())
	if err != nil {
		return 0, err
	}
	defer netdetect.CleanUp(netCtx)

	return netdetect.NumNumaNodes(netCtx), nil
}

// validateScmStorage verifies adequate number of pmem namespaces.
//
// Return slice of pmem block device paths with index in slice equal to NUMA
// node ID.
func (cmd *configGenCmd) validateScmStorage(scmNamespaces storage.ScmNamespaces, getNumNuma numaNumGetter) ([]string, error) {
	minPmems := cmd.NumPmem
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
		cmd.log.Debugf("minimum pmem devices required set to numa count %d", numaCount)
	}

	cmd.log.Debugf("%d scm namespaces detected", len(scmNamespaces))

	if len(scmNamespaces) < minPmems {
		return nil, errors.Errorf("insufficient number of pmem devices, want %d got %d",
			minPmems, len(scmNamespaces))
	}

	// sanity check that each pmem aligns with expected numa node
	var pmemPaths []string
	for idx, ns := range scmNamespaces {
		if int(ns.NumaNode) != idx {
			return nil, errors.Errorf("unexpected numa node for scm %s, want %d got %d",
				ns.BlockDevice, idx, ns.NumaNode)
		}
		pmemPaths = append(pmemPaths, fmt.Sprintf("%s/%s", pmemBdevDir, ns.BlockDevice))
	}

	return pmemPaths, nil
}

// validateNvmeStorage verifies adequate number of ctrlrs per numa node.
//
// Return slice of slices of NVMe SSD PCI addresses. All SSD addresses in group
// will be bound to NUMA node ID specified by the index of the outer slice.
func (cmd *configGenCmd) validateNvmeStorage(ctrlrs storage.NvmeControllers, numaCount int) ([][]string, error) {
	minCtrlrs := cmd.NumNvme
	if minCtrlrs == 0 {
		minCtrlrs = 1 // minimum per numa node
	}

	pciAddrsPerNuma := make([][]string, numaCount)
	for _, ctrlr := range ctrlrs {
		if int(ctrlr.SocketID) > (numaCount - 1) {
			cmd.log.Debugf("skipping nvme device %s with numa %d (in use numa count is %d)",
				ctrlr.PciAddr, ctrlr.SocketID, numaCount)
			continue
		}
		pciAddrsPerNuma[ctrlr.SocketID] = append(pciAddrsPerNuma[ctrlr.SocketID], ctrlr.PciAddr)
	}

	for idx, numaCtrlrs := range pciAddrsPerNuma {
		num := len(numaCtrlrs)
		cmd.log.Debugf("nvme pci bound to numa %d: %+v (%d)", idx, numaCtrlrs, num)

		if num < minCtrlrs {
			return nil, errors.Errorf("insufficient number of nvme devices for numa node %d, want %d got %d",
				idx, minCtrlrs, num)
		}
	}

	return pciAddrsPerNuma, nil
}

// getSingleStorage retrieves the result of storage scan over host list and
// verifies that there is only a single storage set in response which indicates
// that storage hardware setup is homogenous across all hosts.
//
// Return storage for a single host set or error.
func (cmd *configGenCmd) getSingleStorageSet(ctx context.Context, ctlInvoker control.Invoker) (*control.HostStorageSet, error) {
	req := &control.StorageScanReq{}
	req.SetHostList(cmd.hostlist)

	resp, err := control.StorageScan(ctx, ctlInvoker, req)
	if err != nil {
		if cmd.jsonOutputEnabled() {
			return nil, cmd.errorJSON(err)
		}
		return nil, err
	}

	if resp.Errors() != nil {
		var bld strings.Builder
		if err := pretty.PrintResponseErrors(resp, &bld); err != nil {
			return nil, err
		}
		cmd.log.Info(bld.String())

		return nil, resp.Errors()
	}

	// verify homogeneous storage
	numSets := len(resp.HostStorage)
	switch {
	case numSets == 0:
		return nil, errors.New("no host responses")
	case numSets > 1:
		// more than one means non-homogeneous hardware
		var bld strings.Builder
		fmt.Fprintln(&bld,
			"Heterogeneous storage hardware configurations detected, cannot proceed.")
		fmt.Fprintln(&bld, "The following sets of hosts have different storage hardware:")
		for _, hss := range resp.HostStorage {
			fmt.Fprintln(&bld, hss.HostSet.String())
		}
		cmd.log.Info(bld.String())

		return nil, errors.New("storage hardware not consistent across hosts")
	}

	return resp.HostStorage[resp.HostStorage.Keys()[0]], nil
}

// checkStorage validates minimum NVMe and SCM device counts and populates
// ioserver storage config with detected device identifiers if thresholds met.
//
// Return server config populated with ioserver storage or error.
func (cmd *configGenCmd) checkStorage(ctx context.Context, ctlInvoker control.Invoker, getNumNuma numaNumGetter) (*server.Configuration, error) {
	storageSet, err := cmd.getSingleStorageSet(ctx, ctlInvoker)
	if err != nil {
		return nil, err
	}
	scmNamespaces := storageSet.HostStorage.ScmNamespaces
	nvmeControllers := storageSet.HostStorage.NvmeDevices

	cmd.log.Infof("Storage hardware configuration is consistent for hosts %s:\n\t%s\n\t%s",
		storageSet.HostSet.String(), scmNamespaces.Summary(), nvmeControllers.Summary())

	// the pmemPaths is a slice of pmem block devices each pinned to NUMA
	// node ID matching the index in the slice
	pmemPaths, err := cmd.validateScmStorage(scmNamespaces, getNumNuma)
	if err != nil {
		return nil, errors.WithMessage(err, "validating scm storage requirements")
	}

	// bdevLists is a slice of slices of pci addresses for nvme ssd devices
	// pinned to NUMA node ID matching the index in the outer slice
	bdevLists, err := cmd.validateNvmeStorage(nvmeControllers, len(pmemPaths))
	if err != nil {
		return nil, errors.WithMessage(err, "validating nvme storage requirements")
	}

	cfg := server.NewConfiguration()
	for idx, pp := range pmemPaths {
		cfg.Servers = append(cfg.Servers, ioserver.NewConfig().
			WithScmClass("dcpm").
			WithScmDeviceList(pp).
			WithScmMountPoint(fmt.Sprintf("%s%d", scmMountPrefix, idx)).
			WithBdevClass("nvme").
			WithBdevDeviceList(bdevLists[idx]...))
	}

	return cfg, nil
}

func (cmd *configGenCmd) checkNetwork(ctx context.Context, cfg *server.Configuration) (*server.Configuration, error) {
	return cfg, nil // TODO: implement
}

func (cmd *configGenCmd) parseConfig(cfg *server.Configuration) (string, error) {
	bytes, err := yaml.Marshal(cfg)
	if err != nil {
		return "", err
	}

	return string(bytes), nil
}

// Execute is run when configGenCmd activates.
//
// Attempt to auto generate a server config file with populated storage and
// network hardware parameters suitable to be used on all hosts in provided host
// list.
func (cmd *configGenCmd) Execute(_ []string) error {
	if cmd.jsonOutputEnabled() {
		// TODO: consider whether we support json output for config generation
		cmd.log.Info("--json unsupported for config file generation")

		return cmd.errorJSON(nil)
	}

	ctx := context.Background()

	cfg, err := cmd.checkStorage(ctx, cmd.ctlInvoker, getNumaCount)
	if err != nil {
		return err
	}

	cfg, err = cmd.checkNetwork(ctx, cfg)
	if err != nil {
		return err
	}

	if err := cfg.Validate(cmd.log); err != nil {
		return errors.Wrap(err, "validation failed on auto generated config")
	}

	out, err := cmd.parseConfig(cfg)
	if err != nil {
		return err
	}

	cmd.log.Info(out)

	return nil
}
