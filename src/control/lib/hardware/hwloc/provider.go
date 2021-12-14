//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hwloc

import (
	"context"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
)

// NewProvider returns a new hwloc Provider.
func NewProvider(log logging.Logger) *Provider {
	return &Provider{
		api: &api{},
		log: log,
	}
}

// Provider provides access to hwloc topology functionality.
type Provider struct {
	api *api
	log logging.Logger
}

// GetTopology fetches a simplified hardware topology via hwloc.
func (p *Provider) GetTopology(ctx context.Context) (*hardware.Topology, error) {
	ch := make(chan topologyResult)
	go p.getTopologyAsync(ch)

	select {
	case <-ctx.Done():
		return nil, ctx.Err()
	case result := <-ch:
		return result.topology, result.err
	}
}

type topologyResult struct {
	topology *hardware.Topology
	err      error
}

func (p *Provider) getTopologyAsync(ch chan topologyResult) {
	p.log.Debugf("getting topology with hwloc version 0x%x", p.api.compiledVersion())

	topo, cleanup, err := p.initTopology()
	if err != nil {
		ch <- topologyResult{
			err: err,
		}
		return
	}
	defer cleanup()

	nodes, err := p.getNUMANodes(topo)
	if err != nil {
		ch <- topologyResult{
			err: err,
		}
		return
	}

	ch <- topologyResult{
		topology: &hardware.Topology{
			NUMANodes: nodes,
		},
	}
}

// initTopo initializes the hwloc topology and returns it to the caller along with the topology
// cleanup function.
func (p *Provider) initTopology() (*topology, func(), error) {
	if err := checkVersion(p.api); err != nil {
		return nil, nil, err
	}

	p.api.Lock()
	defer p.api.Unlock()

	topo, cleanup, err := p.api.newTopology()
	if err != nil {
		return nil, nil, err
	}
	defer func() {
		if err != nil {
			cleanup()
		}
	}()

	if err = topo.setFlags(); err != nil {
		return nil, nil, err
	}

	if err = topo.load(); err != nil {
		return nil, nil, err
	}

	return topo, cleanup, nil
}

// checkVersion verifies the runtime API is compatible with the compiled version of the API.
func checkVersion(api *api) error {
	version := api.runtimeVersion()
	compVersion := api.compiledVersion()
	if (version >> 16) != (compVersion >> 16) {
		return errors.Errorf("hwloc API incompatible with runtime: compiled for version 0x%x but using 0x%x\n",
			compVersion, version)
	}
	return nil
}

func (p *Provider) getNUMANodes(topo *topology) (map[uint]*hardware.NUMANode, error) {
	coresByNode := p.getCoreCountsPerNodeSet(topo)
	pciDevsByNode := p.getPCIDevsPerNUMANode(topo)

	nodes := make(map[uint]*hardware.NUMANode)

	prevNode := (*object)(nil)
	for {
		numaObj, err := topo.getNextObjByType(objTypeNUMANode, prevNode)
		if err != nil {
			break
		}
		prevNode = numaObj

		nodeStr := numaObj.nodeSet().String()

		id := numaObj.osIndex()
		newNode := &hardware.NUMANode{
			ID:       id,
			NumCores: coresByNode[nodeStr],
		}

		if devs, exists := pciDevsByNode[id]; exists {
			newNode.Devices = devs
		} else {
			newNode.Devices = make(hardware.PCIDevices)
		}

		nodes[id] = newNode
	}

	if len(nodes) == 0 {
		// If hwloc didn't detect any NUMA nodes, create a virtual NUMA node 0
		totalCores := uint(0)
		for _, numCores := range coresByNode {
			totalCores += numCores
		}
		nodes[0] = &hardware.NUMANode{
			ID:       0,
			NumCores: totalCores,
			Devices:  pciDevsByNode[0],
		}
	}

	return nodes, nil
}

func (p *Provider) getCoreCountsPerNodeSet(topo *topology) map[string]uint {
	prevCore := (*object)(nil)
	coresPerNode := make(map[string]uint)
	for {
		coreObj, err := topo.getNextObjByType(objTypeCore, prevCore)
		if err != nil {
			break
		}

		coresPerNode[coreObj.nodeSet().String()]++

		prevCore = coreObj
	}

	return coresPerNode
}

func (p *Provider) getPCIDevsPerNUMANode(topo *topology) map[uint]hardware.PCIDevices {
	pciPerNUMA := make(map[uint]hardware.PCIDevices)

	prev := (*object)(nil)
	for {
		osDev, err := topo.getNextObjByType(objTypeOSDevice, prev)
		if err != nil {
			// end of the list
			break
		}
		prev = osDev

		osDevType, err := osDev.osDevType()
		if err != nil {
			// shouldn't be possible
			p.log.Error(err.Error())
			continue
		}

		switch osDevType {
		case osDevTypeNetwork, osDevTypeOpenFabrics:
			addr := "none"
			if pciDev, err := osDev.getAncestorByType(objTypePCIDevice); err == nil {
				addr, err = pciDev.pciAddr()
				if err != nil {
					// shouldn't be possible
					p.log.Error(err.Error())
				}
			} else {
				// unexpected - network devices should be on the PCI bus
				p.log.Error(err.Error())

			}

			numaID := p.getDeviceNUMANodeID(osDev, topo)

			if _, exists := pciPerNUMA[numaID]; !exists {
				pciPerNUMA[numaID] = make(hardware.PCIDevices)
			}

			pciPerNUMA[numaID].Add(&hardware.Device{
				Name:         osDev.name(),
				Type:         osDevTypeToHardwareDevType(osDevType),
				PCIAddr:      addr,
				NUMAAffinity: numaID,
			})
		}
	}

	return pciPerNUMA
}

func (p *Provider) getDeviceNUMANodeID(dev *object, topo *topology) uint {
	if numaNode, err := dev.getAncestorByType(objTypeNUMANode); err == nil {
		return numaNode.osIndex()
	}

	// If we made it here, it means that we're running in some flavor of a restricted environment
	// which caused the numa node ancestor lookup to fail.  We're not restricted from looking at the
	// cpuset for each numa node, so we can look for an intersection of the node's cpuset with each
	// numa node cpuset until a match is found or we run out of candidates.
	ancestor, err := dev.getNonIOAncestor()
	if err != nil {
		p.log.Errorf(err.Error())
		return 0
	}

	prev := (*object)(nil)
	for {
		numaNode, err := topo.getNextObjByType(objTypeNUMANode, prev)
		if err != nil {
			// end of list
			break
		}
		prev = numaNode

		if ancestor.cpuSet().isSubsetOf(numaNode.cpuSet()) {
			return numaNode.osIndex()
		}
	}

	p.log.Debugf("Unable to determine NUMA socket ID. Using NUMA 0")
	return 0

}

func osDevTypeToHardwareDevType(osType int) hardware.DeviceType {
	switch osType {
	case osDevTypeNetwork:
		return hardware.DeviceTypeNetInterface
	case osDevTypeOpenFabrics:
		return hardware.DeviceTypeOFIDomain
	}

	return hardware.DeviceTypeUnknown
}
