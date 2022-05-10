//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hardware

import (
	"context"
	"sort"

	"github.com/pkg/errors"
)

type (
	// TopologyProvider is an interface for acquiring a system topology.
	TopologyProvider interface {
		GetTopology(context.Context) (*Topology, error)
	}

	// ProcessNUMAProvider is an interface for getting the NUMA node associated with a
	// process ID.
	ProcessNUMAProvider interface {
		GetNUMANodeIDForPID(context.Context, int32) (uint, error)
	}

	// NodeMap maps a node ID to a node.
	NodeMap map[uint]*NUMANode

	// Topology is a hierarchy of hardware devices grouped under NUMA nodes.
	Topology struct {
		// NUMANodes is the set of NUMA nodes mapped by their ID.
		NUMANodes NodeMap `json:"numa_nodes"`
	}
)

// AllDevices returns a map of all system Devices sorted by their name.
func (t *Topology) AllDevices() map[string]*PCIDevice {
	devsByName := make(map[string]*PCIDevice)
	if t == nil {
		return devsByName
	}

	for _, numaNode := range t.NUMANodes {
		for _, devs := range numaNode.PCIDevices {
			for _, dev := range devs {
				devsByName[dev.Name] = dev
			}
		}
	}
	return devsByName
}

// NumNUMANodes gets the number of NUMA nodes in the system topology.
func (t *Topology) NumNUMANodes() int {
	if t == nil {
		return 0
	}
	return len(t.NUMANodes)
}

// NumCoresPerNUMA gets the number of cores per NUMA node.
func (t *Topology) NumCoresPerNUMA() int {
	if t == nil {
		return 0
	}

	for _, numa := range t.NUMANodes {
		return len(numa.Cores)
	}

	return 0
}

// AddDevice adds a device to the topology.
func (t *Topology) AddDevice(numaID uint, device *PCIDevice) error {
	if t == nil {
		return errors.New("nil Topology")
	}

	if device == nil {
		return errors.New("nil PCIDevice")
	}

	if t.NUMANodes == nil {
		t.NUMANodes = make(NodeMap)
	}

	numa, exists := t.NUMANodes[numaID]
	if !exists {
		numa = &NUMANode{
			ID:         numaID,
			Cores:      []CPUCore{},
			PCIDevices: PCIDevices{},
		}
		t.NUMANodes[numaID] = numa
	}

	numa.AddDevice(device)

	return nil
}

// Merge updates the contents of the initial topology from the incoming topology.
func (t *Topology) Merge(newTopo *Topology) error {
	if t == nil {
		return errors.New("nil original Topology")
	}

	if newTopo == nil {
		return errors.New("nil new Topology")
	}

	for numaID, node := range newTopo.NUMANodes {
		if t.NUMANodes == nil {
			t.NUMANodes = make(NodeMap)
		}

		current, exists := t.NUMANodes[numaID]
		if !exists {
			t.NUMANodes[numaID] = node
			continue
		}

		for _, core := range node.Cores {
			found := false
			for _, curCore := range current.Cores {
				if curCore.ID == core.ID {
					found = true
					break
				}
			}

			if !found {
				current.AddCore(core)
			}
		}

		for _, bus := range node.PCIBuses {
			found := false
			for _, curBus := range current.PCIBuses {
				if curBus.HighAddress.Equals(&bus.HighAddress) &&
					curBus.LowAddress.Equals(&bus.LowAddress) {
					found = true
					break
				}
			}

			if !found {
				current.AddPCIBus(bus)
			}
		}

		if current.PCIDevices == nil {
			current.PCIDevices = make(PCIDevices)
		}

		for key, newDevs := range node.PCIDevices {
			oldDevs := current.PCIDevices[key]
			for _, newDev := range newDevs {
				devExists := false
				for _, oldDev := range oldDevs {
					if newDev.Name == oldDev.Name {
						devExists = true

						// Only a couple parameters can be overridden
						if oldDev.Type == DeviceTypeUnknown {
							oldDev.Type = newDev.Type
						}

						if oldDev.LinkSpeed == 0 {
							oldDev.LinkSpeed = newDev.LinkSpeed
						}
					}
				}
				if !devExists {
					if err := current.PCIDevices.Add(newDev); err != nil {
						return err
					}
				}
			}
		}
	}
	return nil
}

type (
	// CPUCore represents a CPU core within a NUMA node.
	CPUCore struct {
		ID       uint      `json:"id"`
		NUMANode *NUMANode `json:"-"`
	}

	// NUMANode represents an individual NUMA node in the system and the devices associated with it.
	NUMANode struct {
		ID         uint       `json:"id"`
		Cores      []CPUCore  `json:"cores"`
		PCIBuses   []*PCIBus  `json:"pci_buses"`
		PCIDevices PCIDevices `json:"pci_devices"`
	}
)

// AddPCIBus adds a PCI bus to the node.
func (n *NUMANode) AddPCIBus(bus *PCIBus) error {
	if n == nil || bus == nil {
		return errors.New("node or bus is nil")
	}

	bus.NUMANode = n
	n.PCIBuses = append(n.PCIBuses, bus)

	return nil
}

// AddDevice adds a PCI device to the node.
func (n *NUMANode) AddDevice(dev *PCIDevice) error {
	if n == nil || dev == nil {
		return errors.New("node or device is nil")
	}
	if n.PCIDevices == nil {
		n.PCIDevices = make(PCIDevices)
	}

	dev.NUMANode = n
	n.PCIDevices.Add(dev)

	for _, bus := range n.PCIBuses {
		if bus.Contains(dev.PCIAddr) {
			return bus.AddDevice(dev)
		}
	}

	return nil
}

// AddCore adds a CPU core to the node.
func (n *NUMANode) AddCore(core CPUCore) error {
	if n == nil {
		return errors.New("node is nil")
	}

	core.NUMANode = n
	n.Cores = append(n.Cores, core)

	return nil
}

// DeviceType indicates the type of a hardware device.
type DeviceType uint

const (
	// DeviceTypeUnknown indicates a device type that is not recognized.
	DeviceTypeUnknown DeviceType = iota
	// DeviceTypeNetInterface indicates a standard network interface.
	DeviceTypeNetInterface
	// DeviceTypeOFIDomain indicates an OpenFabrics domain device.
	DeviceTypeOFIDomain
)

func (t DeviceType) String() string {
	switch t {
	case DeviceTypeNetInterface:
		return "network interface"
	case DeviceTypeOFIDomain:
		return "OFI domain"
	}

	return "unknown device type"
}

// WeightedTopologyProvider is a provider with associated weight to determine order of operations.
// Greater weights indicate higher priority.
type WeightedTopologyProvider struct {
	Provider TopologyProvider
	Weight   int
}

// TopologyFactory is a TopologyProvider that merges results from multiple other
// TopologyProviders.
type TopologyFactory struct {
	providers []TopologyProvider
}

// GetTopology gets a merged master topology from all the topology providers.
func (tf *TopologyFactory) GetTopology(ctx context.Context) (*Topology, error) {
	if tf == nil {
		return nil, errors.New("nil TopologyFactory")
	}

	if len(tf.providers) == 0 {
		return nil, errors.New("no TopologyProviders in TopologyFactory")
	}

	newTopo := &Topology{}
	for _, prov := range tf.providers {
		topo, err := prov.GetTopology(ctx)
		if err != nil {
			return nil, err
		}
		newTopo.Merge(topo)
	}
	return newTopo, nil
}

// NewTopologyFactory creates a TopologyFactory based on the list of weighted topology providers.
func NewTopologyFactory(providers ...*WeightedTopologyProvider) *TopologyFactory {
	sort.Slice(providers, func(i, j int) bool {
		// Higher weight goes first
		return providers[i].Weight > providers[j].Weight
	})

	orderedProviders := make([]TopologyProvider, 0, len(providers))
	for _, wtp := range providers {
		orderedProviders = append(orderedProviders, wtp.Provider)
	}

	return &TopologyFactory{
		providers: orderedProviders,
	}
}
