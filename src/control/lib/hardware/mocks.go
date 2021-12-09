//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hardware

import "fmt"

// MockNUMANode returns a mock NUMA node for testing.
func MockNUMANode(id uint, numCores uint, optOff ...uint) *NUMANode {
	offset := uint(0)
	if len(optOff) == 1 {
		offset = optOff[0]
	}

	node := &NUMANode{
		ID:    id,
		Set:   fmt.Sprintf("0x%08x", id+1),
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
