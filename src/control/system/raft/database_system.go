//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package raft

type (
	// SystemDatabase contains system-level information
	// that must be raft-replicated.
	SystemDatabase struct {
		Attributes map[string]string
	}
)
