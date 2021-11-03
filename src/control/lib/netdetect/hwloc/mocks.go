//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hwloc

type mockAPI struct {
	runtimeVer     uint
	compVer        uint
	numCallsLock   uint
	numCallsUnlock uint
	newTopo        Topology
	newTopoCleanup func()
	newTopoErr     error
}

func (m *mockAPI) Lock() {
	m.numCallsLock++
}

func (m *mockAPI) Unlock() {
	m.numCallsUnlock++
}

func (m *mockAPI) runtimeVersion() uint {
	return m.runtimeVer
}

func (m *mockAPI) compiledVersion() uint {
	return m.compVer
}

func (m *mockAPI) newTopology() (Topology, func(), error) {
	return m.newTopo, m.newTopoCleanup, m.newTopoErr
}

type mockTopology struct {
	setFlagsErr error
	loadErr     error
}

func (m *mockTopology) GetProcCPUBind(pid int32, flags int) (CPUSet, func(), error) {
	return nil, nil, nil
}

func (m *mockTopology) SetFlags() error {
	return m.setFlagsErr
}

func (m *mockTopology) Load() error {
	return m.loadErr
}

func (m *mockTopology) GetObjByDepth(depth int, index uint) (Object, error) {
	return nil, nil
}

func (m *mockTopology) GetTypeDepth(objType ObjType) int {
	return 0
}

func (m *mockTopology) GetNumObjAtDepth(depth int) uint {
	return 0
}
