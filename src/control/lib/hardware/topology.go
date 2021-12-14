//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hardware

import (
	"context"
	"encoding/json"
	"fmt"
	"io"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
	"github.com/daos-stack/daos/src/control/system"
)

type (
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

// PrintTopology prints the topology to the given writer.
func PrintTopology(t *Topology, output io.Writer) error {
	ew := txtfmt.NewErrWriter(output)

	if t == nil {
		fmt.Fprintf(ew, "No topology information available\n")
	}

	for _, numaNode := range t.NUMANodes {
		coreSet := &system.RankSet{}
		for _, core := range numaNode.Cores {
			coreSet.Add(system.Rank(core.ID))
		}

		fmt.Fprintf(ew, "NUMA node %d\n", numaNode.ID)
		fmt.Fprintf(ew, "  CPU cores: %s\n", coreSet)
		fmt.Fprintf(ew, "  PCI devices: \n")
		for addr, devs := range numaNode.PCIDevices {
			fmt.Fprintf(ew, "    %s\n", &addr)
			for _, dev := range devs {
				fmt.Fprintf(ew, "      %s\n", dev)
			}
		}
		fmt.Fprintf(ew, "  PCI buses: \n")
		for _, bus := range numaNode.PCIBuses {
			fmt.Fprintf(ew, "    %s\n", bus)
		}
	}

	return ew.Err
}

type (
	// CPUCore represents a CPU core within a NUMA node.
	CPUCore struct {
		ID       uint      `json:"id"`
		NUMANode *NUMANode `json:"-"`
	}

	// PCIDevice represents an individual hardware device.
	PCIDevice struct {
		Name      string            `json:"name"`
		Type      DeviceType        `json:"type"`
		NUMANode  *NUMANode         `json:"-"`
		Bus       *PCIBus           `json:"-"`
		PCIAddr   common.PCIAddress `json:"pci_address"`
		LinkSpeed float64           `json:"link_speed"`
	}

	// PCIBus represents the root of a PCI bus hierarchy.
	PCIBus struct {
		LowAddress  common.PCIAddress `json:"low_address"`
		HighAddress common.PCIAddress `json:"high_address"`
		NUMANode    *NUMANode         `json:"-"`
		PCIDevices  PCIDevices        `json:"pci_devices"`
	}

	// NUMANode represents an individual NUMA node in the system and the devices associated with it.
	NUMANode struct {
		ID         uint       `json:"id"`
		Cores      []CPUCore  `json:"cores"`
		PCIBuses   []*PCIBus  `json:"pci_buses"`
		PCIDevices PCIDevices `json:"pci_devices"`
	}

	// PCIDevices groups hardware devices by PCI address.
	PCIDevices map[common.PCIAddress][]*PCIDevice
)

// AddDevice adds a PCI device to the bus.
func (b *PCIBus) AddDevice(dev *PCIDevice) {
	if b == nil || dev == nil {
		return
	}
	if !b.Contains(dev.PCIAddr) {
		return
	}
	if b.PCIDevices == nil {
		b.PCIDevices = make(PCIDevices)
	}

	dev.Bus = b
	b.PCIDevices[dev.PCIAddr] = append(b.PCIDevices[dev.PCIAddr], dev)
}

// Contains returns true if the given PCI address is contained within the bus.
func (b *PCIBus) Contains(addr common.PCIAddress) bool {
	if b == nil {
		return false
	}

	return b.LowAddress.Domain == addr.Domain &&
		b.LowAddress.Bus <= addr.Bus &&
		addr.Bus <= b.HighAddress.Bus
}

func (b *PCIBus) String() string {
	if b.LowAddress.Bus == b.HighAddress.Bus {
		return fmt.Sprintf("%s:%s", b.LowAddress.Domain, b.LowAddress.Bus)
	}
	return fmt.Sprintf("%s:[%s-%s]", b.LowAddress.Domain, b.LowAddress.Bus, b.HighAddress.Bus)
}

func (d *PCIDevice) String() string {
	var speedStr string
	if d.LinkSpeed > 0 {
		speedStr = fmt.Sprintf(" @ %.2f GB/s", d.LinkSpeed)
	}
	return fmt.Sprintf("%s %s (%s)%s", &d.PCIAddr, d.Name, d.Type, speedStr)
}

func (pd PCIDevices) MarshalJSON() ([]byte, error) {
	strMap := make(map[string][]*PCIDevice)
	for k, v := range pd {
		strMap[k.String()] = v
	}
	return json.Marshal(strMap)
}

// AddPCIBus adds a PCI bus to the node.
func (n *NUMANode) AddPCIBus(bus *PCIBus) {
	if n == nil || bus == nil {
		return
	}

	bus.NUMANode = n
	n.PCIBuses = append(n.PCIBuses, bus)
}

// WithPCIBuses is a convenience function to add multiple PCI buses to the node.
func (n *NUMANode) WithPCIBuses(buses []*PCIBus) *NUMANode {
	for _, bus := range buses {
		n.AddPCIBus(bus)
	}
	return n
}

// AddDevice adds a PCI device to the node.
func (n *NUMANode) AddDevice(dev *PCIDevice) {
	if n == nil || dev == nil {
		return
	}
	if n.PCIDevices == nil {
		n.PCIDevices = make(PCIDevices)
	}

	dev.NUMANode = n
	n.PCIDevices.Add(dev)

	for _, bus := range n.PCIBuses {
		if bus.Contains(dev.PCIAddr) {
			bus.AddDevice(dev)
			return
		}
	}
}

// WithDevices is a convenience function to add a set of devices to a node.
func (n *NUMANode) WithDevices(devices []*PCIDevice) *NUMANode {
	for _, dev := range devices {
		n.AddDevice(dev)
	}
	return n
}

// AddCore adds a CPU core to the node.
func (n *NUMANode) AddCore(core CPUCore) {
	if n == nil {
		return
	}

	core.NUMANode = n
	n.Cores = append(n.Cores, core)
}

// WithCPUCores is a convenience function to add a set of cores to a node.
func (n *NUMANode) WithCPUCores(cores []CPUCore) *NUMANode {
	for _, core := range cores {
		n.AddCore(core)
	}
	return n
}

// Add adds a device to the PCIDevices.
func (d PCIDevices) Add(dev *PCIDevice) {
	if d == nil || dev == nil {
		return
	}
	addr := dev.PCIAddr
	d[addr] = append(d[addr], dev)
}

// Keys fetches the sorted keys for the map.
func (d PCIDevices) Keys() []*common.PCIAddress {
	set := new(common.PCIAddressSet)
	for k := range d {
		ref := k
		if err := set.Add(&ref); err != nil {
			panic(err)
		}
	}
	return set.Addresses()
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

// TopologyProvider is an interface for acquiring a system topology.
type TopologyProvider interface {
	GetTopology(context.Context) (*Topology, error)
}
