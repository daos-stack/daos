//
// (C) Copyright 2022 Intel Corporation.
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

/*
#include <daos_types.h>
#include <daos_srv/control.h>
*/
import "C"

import "time"

const (
	// DefaultCartTimeout defines the default timeout for cart operations.
	DefaultCartTimeout = 60 * time.Second // Should use CRT_DEFAULT_TIMEOUT_S but it's not exported

	// MaxAttributeNameLength defines the maximum length of an attribute name.
	MaxAttributeNameLength = C.DAOS_ATTR_NAME_MAX
)

const (
	DefaultFilePerm = C.DEFAULT_FILE_PERM
	DefaultDirPerm  = C.DEFAULT_DIR_PERM
)
