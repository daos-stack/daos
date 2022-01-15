//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hardware

// MockNUMANode returns a mock NUMA node for testing.
func MockNUMANode(id uint, numCores uint, optOff ...uint) *NUMANode {
	offset := uint(0)
	if len(optOff) == 1 {
		offset = optOff[0]
	}

	node := &NUMANode{
		ID:    id,
		Cores: make([]CPUCore, numCores),
	}
	for i := uint(0); i < numCores; i++ {
		node.Cores[i] = CPUCore{
			ID:       i + offset,
			NUMANode: node,
		}
	}

	return node
}

// MockPCIAddress returns a mock PCI address for testing.
func MockPCIAddress(fields ...uint8) *PCIAddress {
	switch len(fields) {
	case 0:
		fields = []uint8{0, 0, 0, 0}
	case 1:
		fields = append(fields, []uint8{0, 0, 0}...)
	case 2:
		fields = append(fields, []uint8{0, 0}...)
	case 3:
		fields = append(fields, 0)
	}

	return &PCIAddress{
		Domain:   uint16(fields[0]),
		Bus:      fields[1],
		Device:   fields[2],
		Function: fields[3],
	}
}

// WithPCIBuses is a convenience function to add multiple PCI buses to the node.
// NB: If AddPCIBus fails, a panic will ensue.
func (n *NUMANode) WithPCIBuses(buses []*PCIBus) *NUMANode {
	for _, bus := range buses {
		if err := n.AddPCIBus(bus); err != nil {
			panic(err)
		}
	}
	return n
}

// WithDevices is a convenience function to add a set of devices to a node.
// NB: If AddDevice fails, a panic will ensue.
func (n *NUMANode) WithDevices(devices []*PCIDevice) *NUMANode {
	for _, dev := range devices {
		if err := n.AddDevice(dev); err != nil {
			panic(err)
		}
	}
	return n
}

// WithCPUCores is a convenience function to add a set of cores to a node.
// NB: If AddCore fails, a panic will ensue.
func (n *NUMANode) WithCPUCores(cores []CPUCore) *NUMANode {
	for _, core := range cores {
		if err := n.AddCore(core); err != nil {
			panic(err)
		}
	}
	return n
}
