//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hardware

import (
	"context"
	"fmt"
	"sync"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
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

// WithBlockDevices is a convenience function to add a set of block devices to a node.
// NB: If AddBlockDevice fails, a panic will ensue.
func (n *NUMANode) WithBlockDevices(devices []*BlockDevice) *NUMANode {
	for _, dev := range devices {
		if err := n.AddBlockDevice(dev); err != nil {
			panic(err)
		}
	}
	return n
}

// GetMockFabricScannerConfig gets a FabricScannerConfig for testing.
func GetMockFabricScannerConfig() *FabricScannerConfig {
	return &FabricScannerConfig{
		TopologyProvider: &MockTopologyProvider{GetTopoReturn: &Topology{}},
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

func (m *MockFabricInterfaceProvider) GetFabricInterfaces(_ context.Context, _ string) (*FabricInterfaceSet, error) {
	return m.GetFabricReturn, m.GetFabricErr
}

type multiCallFabricInterfaceProvider struct {
	called int
	prefix string
}

func testFabricDevName(prefix string, i int) string {
	return fmt.Sprintf("%s%02d", prefix, i)
}

func (p *multiCallFabricInterfaceProvider) GetFabricInterfaces(_ context.Context, provider string) (*FabricInterfaceSet, error) {
	p.called++
	name := testFabricDevName(p.prefix, p.called)
	return NewFabricInterfaceSet(&FabricInterface{
		Name:          name,
		NetInterfaces: common.NewStringSet(name),
		Providers:     NewFabricProviderSet(&FabricProvider{Name: provider}),
	}), nil
}

func expectedMultiCallFIProviderResult(prefix string, ndc NetDevClass, providers ...string) *FabricInterfaceSet {
	set := NewFabricInterfaceSet()
	for i, p := range providers {
		name := testFabricDevName(prefix, i+1)
		set.Update(&FabricInterface{
			Name:          name,
			NetInterfaces: common.NewStringSet(name),
			Providers:     NewFabricProviderSet(&FabricProvider{Name: p}),
			DeviceClass:   ndc,
		})
	}
	return set
}

// MockGetNetDevClassResult is used to set up a MockNetDevClassProvider's results for GetNetDevClass.
type MockGetNetDevClassResult struct {
	ExpInput string
	NDC      NetDevClass
	Err      error
}

// MockNetDevClassProvider is a NetDevClassProvider for testing.
type MockNetDevClassProvider struct {
	mutex                sync.Mutex
	GetNetDevClassReturn []MockGetNetDevClassResult
	GetNetDevClassCalled int
}

func (m *MockNetDevClassProvider) GetNetDevClass(in string) (NetDevClass, error) {
	if len(m.GetNetDevClassReturn) == 0 {
		return 0, nil
	}

	m.mutex.Lock()
	defer m.mutex.Unlock()
	result := m.GetNetDevClassReturn[m.GetNetDevClassCalled%len(m.GetNetDevClassReturn)]
	if result.ExpInput != "" && in != result.ExpInput {
		return 0, errors.Errorf("MOCK: unexpected input %q != %q", in, result.ExpInput)
	}
	m.GetNetDevClassCalled++
	return result.NDC, result.Err
}

// MockFabricInterfaceSetBuilder is a FabricInterfaceSetBuilder for testing.
type MockFabricInterfaceSetBuilder struct {
	BuildPartCalled    int
	BuildPartUpdateFis func(*FabricInterfaceSet)
	BuildPartReturn    error
}

func (m *MockFabricInterfaceSetBuilder) BuildPart(_ context.Context, fis *FabricInterfaceSet) error {
	m.BuildPartCalled++
	if m.BuildPartUpdateFis != nil {
		m.BuildPartUpdateFis(fis)
	}
	return m.BuildPartReturn
}

// MockNetDevStateResult is a structure for injecting results into MockNetDevStateProvider.
type MockNetDevStateResult struct {
	State NetDevState
	Err   error
}

// MockNetDevStateProvider is a fake NetDevStateProvider for testing.
type MockNetDevStateProvider struct {
	sync.Mutex
	GetStateReturn []MockNetDevStateResult
	GetStateCalled []string
}

func (m *MockNetDevStateProvider) GetNetDevState(iface string) (NetDevState, error) {
	m.Lock()
	defer m.Unlock()
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

// MockFabricScannerConfig provides parameters for constructing a mock fabric scanner.
type MockFabricScannerConfig struct {
	ScanResult *FabricInterfaceSet
}

// MockFabricScanner generates a mock FabricScanner for testing.
func MockFabricScanner(log logging.Logger, cfg *MockFabricScannerConfig) *FabricScanner {
	config := GetMockFabricScannerConfig()
	providers := make([]string, 0)
	fiList := make([]*FabricInterface, 0)
	for _, fi := range cfg.ScanResult.byName {
		providers = append(providers, fi.Providers.byName.keys()...)
		fiList = append(fiList, fi)
	}
	builders := []FabricInterfaceSetBuilder{
		&MockFabricInterfaceSetBuilder{
			BuildPartUpdateFis: func(fis *FabricInterfaceSet) {
				for _, fi := range fiList {
					fis.Update(fi)
				}
			},
		},
	}
	return &FabricScanner{
		log:       log,
		config:    config,
		builders:  builders,
		providers: common.NewStringSet(providers...),
	}
}
