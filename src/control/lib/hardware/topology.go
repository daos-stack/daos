//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hardware

import (
	"context"
	"sort"
)

// Topology is a hierarchy of hardware devices grouped under NUMA nodes.
type Topology struct {
	// NUMANodes is the set of NUMA nodes mapped by their ID.
	NUMANodes map[uint]*NUMANode `json:"numa_nodes"`
}

// AllDevices returns a map of all system Devices sorted by their name.
func (t *Topology) AllDevices() map[string]*Device {
	devsByName := make(map[string]*Device)
	if t == nil {
		return devsByName
	}

	for _, numaNode := range t.NUMANodes {
		for _, devs := range numaNode.Devices {
			for _, dev := range devs {
				devsByName[dev.Name] = dev
			}
		}
	}
	return devsByName
}

type (
	// NUMANode represents an individual NUMA node in the system and the devices associated with it.
	NUMANode struct {
		ID       uint       `json:"id"`
		NumCores uint       `json:"num_cores"`
		Devices  PCIDevices `json:"devices"`
	}

	// PCIDevices groups hardware devices by PCI address.
	PCIDevices map[string][]*Device
)

// Add adds a device to the PCIDevices.
func (d PCIDevices) Add(dev *Device) {
	if d == nil || dev == nil {
		return
	}
	addr := dev.PCIAddr
	d[addr] = append(d[addr], dev)
}

// Keys fetches the sorted keys for the map.
func (d PCIDevices) Keys() []string {
	keys := make([]string, 0, len(d))
	for k := range d {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
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

// Device represents an individual hardware device.
type Device struct {
	Name         string     `json:"name"`
	Type         DeviceType `json:"type"`
	NUMAAffinity uint       `json:"numa_affinity"`
	PCIAddr      string     `json:"pci_address"`
}

// TopologyProvider is an interface for acquiring a system topology.
type TopologyProvider interface {
	GetTopology(context.Context) (*Topology, error)
}
