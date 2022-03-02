//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import "time"

const (
	// DefaultCartTimeout defines the default timeout for cart operations.
	DefaultCartTimeout = 60 * time.Second // Should use CRT_DEFAULT_TIMEOUT_S but it's not exported
)
