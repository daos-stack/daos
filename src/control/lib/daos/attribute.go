//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

type (
	// Attribute is a pool or container attribute.
	Attribute struct {
		Name  string `json:"name"`
		Value []byte `json:"value,omitempty"`
	}
)
