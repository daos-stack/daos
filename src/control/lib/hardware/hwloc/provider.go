//
// (C) Copyright 2021-2022 Intel Corporation.
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

type ctxKey string

const (
	topoKey    ctxKey = "hwlocTopology"
	cleanupKey ctxKey = "hwlocFreeTopology"
)

// CacheContext adds a cache of the hwloc topology to the provided context.
func CacheContext(parent context.Context, log logging.Logger) (context.Context, error) {
	topo, cleanup, err := NewProvider(log).initTopology()
	if err != nil {
		return nil, err
	}

	topoCtx := context.WithValue(parent, topoKey, topo)
	return context.WithValue(topoCtx, cleanupKey, cleanup), nil
}

// Cleanup frees the hwloc cache in the context, if any.
func Cleanup(ctx context.Context) {
	cleanup, ok := ctx.Value(cleanupKey).(func())
	if !ok {
		return
	}
	cleanup()
}

func topologyFromContext(ctx context.Context) (*topology, error) {
	topo, ok := ctx.Value(topoKey).(*topology)
	if !ok {
		return nil, errors.New("no topology cached in context")
	}

	return topo, nil
}

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
	go p.getTopologyAsync(ctx, ch)

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

func (p *Provider) getTopologyAsync(ctx context.Context, ch chan topologyResult) {
	p.log.Debugf("getting topology with hwloc version 0x%x", p.api.compiledVersion())

	topo, cleanup, err := p.getRawTopology(ctx)
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

func (p *Provider) getRawTopology(ctx context.Context) (*topology, func(), error) {
	if topo, err := topologyFromContext(ctx); err == nil {
		// NB: If we're using the cached topology, we needn't worry about freeing it now.
		// The caller that initialized the context will clean it up when they're done
		// with it.
		return topo, func() {}, nil
	}

	return p.initTopology()
}

// initTopo initializes the hwloc topology and returns it to the caller along with the topology
// cleanup function.
func (p *Provider) initTopology() (*topology, func(), error) {
	if err := checkVersion(p.api); err != nil {
		return nil, nil, err
	}

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

func (p *Provider) getNUMANodes(topo *topology) (hardware.NodeMap, error) {
	nodes := make(hardware.NodeMap)

	prevNode := (*object)(nil)
	for {
		numaObj, err := topo.getNextObjByType(objTypeNUMANode, prevNode)
		if err != nil {
			break
		}
		prevNode = numaObj

		id := numaObj.osIndex()
		newNode := &hardware.NUMANode{
			ID: id,
		}

		nodes[id] = newNode
	}

	if len(nodes) == 0 {
		// If hwloc didn't detect any NUMA nodes, create a virtual NUMA node 0
		nodes[0] = &hardware.NUMANode{
			ID: 0,
		}
	}

	if err := p.getCoresPerNodeSet(topo, nodes); err != nil {
		return nil, err
	}
	if err := p.getPCIBridgesPerNUMANode(topo, nodes); err != nil {
		return nil, err
	}
	if err := p.getPCIDevsPerNUMANode(topo, nodes); err != nil {
		return nil, err
	}

	return nodes, nil
}

func (p *Provider) getCoresPerNodeSet(topo *topology, nodes hardware.NodeMap) error {
	if topo == nil || nodes == nil {
		return errors.New("nil topology or nodes")
	}

	prevCore := (*object)(nil)
	for {
		coreObj, err := topo.getNextObjByType(objTypeCore, prevCore)
		if err != nil {
			break
		}

		numaID := p.getDeviceNUMANodeID(coreObj, topo)
		for _, node := range nodes {
			if numaID != node.ID {
				continue
			}
			node.AddCore(hardware.CPUCore{
				ID: coreObj.logicalIndex(),
			})
		}

		prevCore = coreObj
	}

	return nil
}

func (p *Provider) getPCIBridgesPerNUMANode(topo *topology, nodes hardware.NodeMap) error {
	if topo == nil || nodes == nil {
		return errors.New("nil topology or nodes")
	}

	var bus *hardware.PCIBus
	prev := (*object)(nil)
	for {
		bridge, err := topo.getNextObjByType(objTypeBridge, prev)
		if err != nil {
			// end of the list
			break
		}
		prev = bridge

		bridgeType, err := bridge.bridgeType()
		if err != nil {
			return err
		}

		lo, hi, err := bridge.busRange()
		if err != nil {
			return err
		}

		bus = &hardware.PCIBus{
			LowAddress:  *lo,
			HighAddress: *hi,
		}

		numaID := p.getDeviceNUMANodeID(bridge, topo)
		switch bridgeType {
		case bridgeTypeHost:
			for _, node := range nodes {
				if node.ID != numaID {
					continue
				}
				node.AddPCIBus(bus)
			}
		case bridgeTypePCI:
			if bus == nil {
				return errors.New("unexpected PCI bridge before host bridge")
			}
			// TODO: Add secondary buses, if relevant.
		default:
			return errors.Errorf("unexpected bridge type %d", bridgeType)
		}
	}

	return nil
}

func (p *Provider) getPCIDevsPerNUMANode(topo *topology, nodes hardware.NodeMap) error {
	if topo == nil || nodes == nil {
		return errors.New("nil topology or nodes")
	}

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
			return err
		}

		var addr *hardware.PCIAddress
		var linkSpeed float64
		switch osDevType {
		case osDevTypeBlock, osDevTypeNetwork, osDevTypeOpenFabrics:
			if pciDev, err := osDev.getAncestorByType(objTypePCIDevice); err == nil {
				addr, err = pciDev.pciAddr()
				if err != nil {
					return err
				}
				linkSpeed, err = pciDev.linkSpeed()
				if err != nil {
					return err
				}
			} else {
				// unexpected - these devices should be on the PCI bus
				p.log.Error(err.Error())
				continue
			}
		default:
			continue
		}

		numaID := p.getDeviceNUMANodeID(osDev, topo)
		for _, node := range nodes {
			if node.ID != numaID {
				continue
			}
			node.AddDevice(&hardware.PCIDevice{
				Name:      osDev.name(),
				Type:      osDevTypeToHardwareDevType(osDevType),
				PCIAddr:   *addr,
				LinkSpeed: linkSpeed,
			})
		}
	}

	return nil
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

	p.log.Debugf("Unable to determine NUMA socket ID for device %q, using NUMA 0", dev.name())
	return 0

}

func osDevTypeToHardwareDevType(osType int) hardware.DeviceType {
	switch osType {
	case osDevTypeNetwork:
		return hardware.DeviceTypeNetInterface
	case osDevTypeOpenFabrics:
		return hardware.DeviceTypeOFIDomain
	case osDevTypeBlock:
		return hardware.DeviceTypeBlock
	}

	return hardware.DeviceTypeUnknown
}

type numaResult struct {
	numaNode uint
	err      error
}

// GetNUMANodeIDForPID fetches the NUMA node ID associated with a given process ID.
func (p *Provider) GetNUMANodeIDForPID(ctx context.Context, pid int32) (uint, error) {
	ch := make(chan numaResult)
	go p.getNUMANodeIDForPIDAsync(ctx, pid, ch)

	select {
	case <-ctx.Done():
		return 0, ctx.Err()
	case result := <-ch:
		return result.numaNode, result.err
	}
}

func (p *Provider) getNUMANodeIDForPIDAsync(ctx context.Context, pid int32, ch chan numaResult) {
	topo, cleanupTopo, err := p.getRawTopology(ctx)
	if err != nil {
		ch <- numaResult{err: errors.Wrap(err, "initializing topology")}
		return
	}
	defer cleanupTopo()

	numNUMANodes := topo.getNumObjByType(objTypeNUMANode)
	if numNUMANodes == 0 {
		// If there are no NUMA nodes detected, there's nothing to do here.
		ch <- numaResult{err: hardware.ErrNoNUMANodes}
		return
	}

	cpuSet, cleanupCPUSet, err := topo.getProcessCPUSet(pid, 0)
	if err != nil {
		ch <- numaResult{err: errors.Wrapf(err, "getting CPU set for PID %d", pid)}
		return
	}
	defer cleanupCPUSet()

	node, err := p.findNUMANodeWithCPUSet(topo, cpuSet)
	if err != nil {
		ch <- numaResult{err: err}
		return
	}

	ch <- numaResult{numaNode: node}
}

func (p *Provider) findNUMANodeWithCPUSet(topo *topology, cpuSet *cpuSet) (uint, error) {
	nodeSet, cleanupNodeSet, err := cpuSet.toNodeSet()
	if err != nil {
		return 0, errors.Wrap(err, "converting CPU set to NUMA node set")
	}
	defer cleanupNodeSet()

	currentNode, err := topo.getNextObjByType(objTypeNUMANode, nil)
	for err == nil {
		if nodeSet.intersects(currentNode.nodeSet()) || cpuSet.intersects(currentNode.cpuSet()) {
			return currentNode.osIndex(), nil
		}

		currentNode, err = topo.getNextObjByType(objTypeNUMANode, currentNode)
	}

	return 0, errors.Errorf("no NUMA node could be associated with CPU set")
}
