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
