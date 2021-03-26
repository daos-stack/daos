//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package drpc

import "unsafe"

// #include <daos_prop.h>
// #include <daos/policy.h>
import "C"

const (
	// PoolPropertyLabel is a string that a user can associate with a pool.
	PoolPropertyLabel = C.DAOS_PROP_PO_LABEL
	// PoolPropertyACL is the Access Control List for a pool.
	PoolPropertyACL = C.DAOS_PROP_PO_ACL
	// PoolPropertyReservedSpace is the ratio of space that can be reserved
	// on each target for rebuild purposes.
	PoolPropertyReservedSpace = C.DAOS_PROP_PO_SPACE_RB
	// PoolPropertySelfHealing defines the self-healing behavior of the pool.
	PoolPropertySelfHealing = C.DAOS_PROP_PO_SELF_HEAL
	// PoolPropertySpaceReclaim defines the free space reclamation behavior of the pool.
	PoolPropertySpaceReclaim = C.DAOS_PROP_PO_RECLAIM
	// PoolPropertyOwner is the user who acts as the owner of the pool.
	PoolPropertyOwner = C.DAOS_PROP_PO_OWNER
	// PoolPropertyOwnerGroup is the group that acts as the owner of the pool.
	PoolPropertyOwnerGroup = C.DAOS_PROP_PO_OWNER_GROUP
	// PoolPropertyECCellSize is the EC Cell size.
	PoolPropertyECCellSize = C.DAOS_PROP_PO_EC_CELL_SZ
	// PoolPropertyPolicy is the tiering policy set for a pool
	PoolPropertyPolicy = C.DAOS_PROP_PO_POLICY
)

const (
	// PoolSpaceReclaimDisabled sets the PoolPropertySpaceReclaim property to disabled.
	PoolSpaceReclaimDisabled = C.DAOS_RECLAIM_DISABLED
	// PoolSpaceReclaimLazy sets the PoolPropertySpaceReclaim property to lazy.
	PoolSpaceReclaimLazy = C.DAOS_RECLAIM_LAZY
	// PoolSpaceReclaimSnapshot sets the PoolPropertySpaceReclaim property to snapshot.
	PoolSpaceReclaimSnapshot = C.DAOS_RECLAIM_SNAPSHOT
	// PoolSpaceReclaimBatch sets the PoolPropertySpaceReclaim property to batch.
	PoolSpaceReclaimBatch = C.DAOS_RECLAIM_BATCH
	// PoolSpaceReclaimTime sets the PoolPropertySpaceReclaim property to time.
	PoolSpaceReclaimTime = C.DAOS_RECLAIM_TIME
)

const (
	// PoolSelfHealingAutoExclude sets the self-healing strategy to auto-exclude.
	PoolSelfHealingAutoExclude = C.DAOS_SELF_HEAL_AUTO_EXCLUDE
	// PoolSelfHealingAutoRebuild sets the self-healing strategy to auto-rebuild.
	PoolSelfHealingAutoRebuild = C.DAOS_SELF_HEAL_AUTO_REBUILD
)

// LabelIsValid checks a label to verify that it meets length/content
// requirements.
func LabelIsValid(label string) bool {
	cLabel := C.CString(label)
	defer C.free(unsafe.Pointer(cLabel))

	return bool(C.daos_label_is_valid(cLabel))
}

type PoolPolicy uint32

const (
	// PoolPolicyDefault sets the pool's policy to io_size
	PoolPolicyIoSize PoolPolicy = C.DAOS_MEDIA_POLICY_IO_SIZE
	// PoolPolicyDefault sets the pool's policy to write_intensivity
	PoolPolicyWriteIntensivity PoolPolicy = C.DAOS_MEDIA_POLICY_WRITE_INTENSIVITY
)
