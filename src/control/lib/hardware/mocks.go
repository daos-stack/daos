//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hardware

import (
	"context"

	"github.com/pkg/errors"
)

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

// GetMockFabricScannerConfig gets a FabricScannerConfig for testing.
func GetMockFabricScannerConfig() *FabricScannerConfig {
	return &FabricScannerConfig{
		TopologyProvider: &MockTopologyProvider{},
		FabricInterfaceProviders: []FabricInterfaceProvider{
			&MockFabricInterfaceProvider{},
		},
		NetDevClassProvider: &MockNetDevClassProvider{},
	}
}

// MockTopologyProvider is a TopologyProvider for testing.
type MockTopologyProvider struct {
	GetTopoReturn *Topology
	GetTopoErr    error
}

func (m *MockTopologyProvider) GetTopology(_ context.Context) (*Topology, error) {
	return m.GetTopoReturn, m.GetTopoErr
}

// MockFabricInterfaceProvider is a FabricInterfaceProvider for testing.
type MockFabricInterfaceProvider struct {
	GetFabricReturn *FabricInterfaceSet
	GetFabricErr    error
}

func (m *MockFabricInterfaceProvider) GetFabricInterfaces(_ context.Context) (*FabricInterfaceSet, error) {
	return m.GetFabricReturn, m.GetFabricErr
}

// MockGetNetDevClassResult is used to set up a MockNetDevClassProvider's results for GetNetDevClass.
type MockGetNetDevClassResult struct {
	ExpInput string
	NDC      NetDevClass
	Err      error
}

// MockNetDevClassProvider is a NetDevClassProvider for testing.
type MockNetDevClassProvider struct {
	GetNetDevClassReturn []MockGetNetDevClassResult
	GetNetDevClassCalled int
}

func (m *MockNetDevClassProvider) GetNetDevClass(in string) (NetDevClass, error) {
	if len(m.GetNetDevClassReturn) == 0 {
		return 0, nil
	}

	result := m.GetNetDevClassReturn[m.GetNetDevClassCalled%len(m.GetNetDevClassReturn)]
	if in != result.ExpInput {
		return 0, errors.Errorf("MOCK: unexpected input %q != %q", in, result.ExpInput)
	}
	m.GetNetDevClassCalled++
	return result.NDC, result.Err
}

type mockFabricInterfaceSetBuilder struct {
	buildPartCalled int
	buildPartReturn error
}

func (m *mockFabricInterfaceSetBuilder) BuildPart(_ context.Context, _ *FabricInterfaceSet) error {
	m.buildPartCalled++
	return m.buildPartReturn
}

// MockNetDevStateResult is a structure for injecting results into MockNetDevStateProvider.
type MockNetDevStateResult struct {
	State NetDevState
	Err   error
}

// MockNetDevStateProvider is a fake NetDevStateProvider for testing.
type MockNetDevStateProvider struct {
	GetStateReturn []MockNetDevStateResult
	GetStateCalled []string
}

func (m *MockNetDevStateProvider) GetNetDevState(iface string) (NetDevState, error) {
	m.GetStateCalled = append(m.GetStateCalled, iface)

	if len(m.GetStateReturn) == 0 {
		return NetDevStateReady, nil
	}

	idx := len(m.GetStateCalled) - 1
	if idx >= len(m.GetStateReturn) {
		idx = len(m.GetStateReturn) - 1
	}
	return m.GetStateReturn[idx].State, m.GetStateReturn[idx].Err
}
