//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hardware

import (
	"context"
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
	NDC NetDevClass
	Err error
}

// MockNetDevClassProvider is a NetDevClassProvider for testing.
type MockNetDevClassProvider struct {
	GetNetDevClassReturn []MockGetNetDevClassResult
	GetNetDevClassCalled int
}

func (m *MockNetDevClassProvider) GetNetDevClass(string) (NetDevClass, error) {
	if len(m.GetNetDevClassReturn) == 0 {
		return Netrom, nil
	}

	result := m.GetNetDevClassReturn[m.GetNetDevClassCalled%len(m.GetNetDevClassReturn)]
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
