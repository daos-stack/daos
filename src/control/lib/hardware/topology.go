//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hardware

import (
	"context"

	"github.com/pkg/errors"
)

type (
	// TopologyProvider is an interface for acquiring a system topology.
	TopologyProvider interface {
		GetTopology(context.Context) (*Topology, error)
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
