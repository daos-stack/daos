//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import (
	"math"
	"unsafe"
)

/*
#cgo LDFLAGS: -ldaos_common -lgurt -lcart
#include <daos_prop.h>
#include <daos_pool.h>
#include <daos/object.h>
#include <daos/pool_map.h>
#include <daos/cont_props.h>
#include <daos_srv/policy.h>
#include <daos_srv/control.h>
*/
import "C"

const (
	// MaxLabelLength is the maximum length of a label.
	MaxLabelLength = C.DAOS_PROP_LABEL_MAX_LEN
)

const (
	// PropEntryAllocedOID is the highest allocated OID.
	PropEntryAllocedOID = C.DAOS_PROP_ENTRY_ALLOCED_OID
	// PropEntryChecksum is the checksum property.
	PropEntryChecksum = C.DAOS_PROP_ENTRY_CKSUM
	// PropEntryChecksumSize is the checksum size property.
	PropEntryChecksumSize = C.DAOS_PROP_ENTRY_CKSUM_SIZE
	// PropEntryCompression is the compression property.
	PropEntryCompression = C.DAOS_PROP_ENTRY_COMPRESS
	// PropEntryDedupe is the dedupe property.
	PropEntryDedupe = C.DAOS_PROP_ENTRY_DEDUP
	// PropEntryDedupThreshold is the dedupe threshold property.
	PropEntryDedupeThreshold = C.DAOS_PROP_ENTRY_DEDUP_THRESHOLD
	// PropEntryECCellSize is the EC cell size property.
	PropEntryECCellSize = C.DAOS_PROP_ENTRY_EC_CELL_SZ
	// PropEntryECPerfDomainAff is the EC performance domain affinity property.
	PropEntryECPerfDomainAff = C.DAOS_PROP_ENTRY_EC_PDA
	// PropEntryEncryption is the encryption property.
	PropEntryEncryption = C.DAOS_PROP_ENTRY_ENCRYPT
	// PropEntryGlobalVersion is the global version property.
	PropEntryGlobalVersion = C.DAOS_PROP_ENTRY_GLOBAL_VERSION
	// PropEntryObjectVersion is the object layout version property.
	PropEntryObjectVersion = C.DAOS_PROP_ENTRY_OBJ_VERSION
	// PropEntryGroup is the group property.
	PropEntryGroup = C.DAOS_PROP_ENTRY_GROUP
	// PropEntryLabel is the label property.
	PropEntryLabel = C.DAOS_PROP_ENTRY_LABEL
	// PropEntryLayout is the layout property.
	PropEntryLayoutType = C.DAOS_PROP_ENTRY_LAYOUT_TYPE
	// PropEntryLayoutVersion is the layout version property.
	PropEntryLayoutVersion = C.DAOS_PROP_ENTRY_LAYOUT_VER
	// PropEntryOwner is the owner property.
	PropEntryOwner = C.DAOS_PROP_ENTRY_OWNER
	// PropEntryRedunFactor is the redundancy factor property.
	PropEntryRedunFactor = C.DAOS_PROP_ENTRY_REDUN_FAC
	// PropEntryRedunLevel is the redundancy level property.
	PropEntryRedunLevel = C.DAOS_PROP_ENTRY_REDUN_LVL
	// PropEntryRedunPerfDomainAff is the redundancy performance domain affinity property.
	PropEntryRedunPerfDomainAff = C.DAOS_PROP_ENTRY_RP_PDA
	// PropEntrySnapshotMax is the snapshot max property.
	PropEntrySnapshotMax = C.DAOS_PROP_ENTRY_SNAPSHOT_MAX
	// PropEntryServerChecksum is the server checksum property.
	PropEntryServerChecksum = C.DAOS_PROP_ENTRY_SRV_CKSUM
	// PropEntryStatus is the status property.
	PropEntryStatus = C.DAOS_PROP_ENTRY_STATUS
)

const (
	// PoolPropertyMin before any pool property
	PoolPropertyMin = C.DAOS_PROP_PO_MIN
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
	// PoolPropertyRedunFac defines redundancy factor of the pool.
	PoolPropertyRedunFac = C.DAOS_PROP_PO_REDUN_FAC
	// PoolPropertyECPda is performance domain affinity level of EC object.
	PoolPropertyECPda = C.DAOS_PROP_PO_EC_PDA
	// PoolPropertyRPPda is performance domain affinity level of replicated object.
	PoolPropertyRPPda = C.DAOS_PROP_PO_RP_PDA
	// PoolPropertyPolicy is the tiering policy set for a pool
	PoolPropertyPolicy = C.DAOS_PROP_PO_POLICY
	//PoolPropertyGlobalVersion is aggregation of pool/container/object/keys version.
	PoolPropertyGlobalVersion = C.DAOS_PROP_PO_GLOBAL_VERSION
	//PoolPropertyUpgradeStatus is pool upgrade status
	PoolPropertyUpgradeStatus = C.DAOS_PROP_PO_UPGRADE_STATUS
	// PoolPropertyScrubMode Checksum scrubbing schedule
	PoolPropertyScrubMode = C.DAOS_PROP_PO_SCRUB_MODE
	// PoolPropertyScrubFreq Checksum scrubbing frequency
	PoolPropertyScrubFreq = C.DAOS_PROP_PO_SCRUB_FREQ
	// PoolPropertyScrubThresh Checksum scrubbing threshold
	PoolPropertyScrubThresh = C.DAOS_PROP_PO_SCRUB_THRESH
	// PoolPropertySvcRedunFac defines redundancy factor of the pool service.
	PoolPropertySvcRedunFac = C.DAOS_PROP_PO_SVC_REDUN_FAC
	// PoolPropertySvcList is the list of pool service replicas.
	PoolPropertySvcList = C.DAOS_PROP_PO_SVC_LIST
	// PoolPropertyCheckpointMode defines the behavior of WAL checkpoints
	PoolPropertyCheckpointMode = C.DAOS_PROP_PO_CHECKPOINT_MODE
	// PoolPropertyCheckpointFreq defines the frequency of timed WAL checkpoints
	PoolPropertyCheckpointFreq = C.DAOS_PROP_PO_CHECKPOINT_FREQ
	// PoolPropertyCheckpointThresh defines the size threshold to trigger WAL checkpoints
	PoolPropertyCheckpointThresh = C.DAOS_PROP_PO_CHECKPOINT_THRESH
	//PoolPropertyPerfDomain is pool performance domain
	PoolPropertyPerfDomain = C.DAOS_PROP_PO_PERF_DOMAIN
	//PoolPropertyReintMode is pool reintegration mode
	PoolPropertyReintMode = C.DAOS_PROP_PO_REINT_MODE
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

const (
	// ECCellMin defines the minimum-allowaable EC cell size.
	ECCellMin = C.DAOS_EC_CELL_MIN
	// ECCellMax defines the maximum-allowaable EC cell size.
	ECCellMax = C.DAOS_EC_CELL_MAX
	// ECCellDefault defines the default EC cell size.
	ECCellDefault = C.DAOS_EC_CELL_DEF
)

const (
	// MediaTypeScm is the media type for SCM.
	MediaTypeScm = C.DAOS_MEDIA_SCM
	// MediaTypeNvme is the media type for NVMe.
	MediaTypeNvme = C.DAOS_MEDIA_NVME
)

const (
	// PoolUpgradeStatusNotStarted indicates pool upgrading not started yet.
	PoolUpgradeStatusNotStarted = C.DAOS_UPGRADE_STATUS_NOT_STARTED
	// PoolUpgradeStatusInProgress defines pool upgrading is in progress.
	PoolUpgradeStatusInProgress = C.DAOS_UPGRADE_STATUS_IN_PROGRESS
	//PoolUpgradeStatusCompleted defines pool upgrading completed last time.
	PoolUpgradeStatusCompleted = C.DAOS_UPGRADE_STATUS_COMPLETED
	//PoolUpgradeStatusFailed defines pool upgrading operation failed.
	PoolUpgradeStatusFailed = C.DAOS_UPGRADE_STATUS_FAILED
)

const (
	// PoolRedunFacMax defines the maximum value of PoolPropertyRedunFac.
	PoolRedunFacMax = C.DAOS_PROP_PO_REDUN_FAC_MAX
	// PoolRedunFacDefault defines the default value of PoolPropertyRedunFac.
	PoolRedunFacDefault = C.DAOS_PROP_PO_REDUN_FAC_DEFAULT
	// PoolSvcRedunFacMax defines the maximum value of PoolPropertySvcRedunFac.
	PoolSvcRedunFacMax = C.DAOS_PROP_PO_SVC_REDUN_FAC_MAX
	// PoolSvcRedunFacDefault defines the default value of PoolPropertySvcRedunFac.
	PoolSvcRedunFacDefault = C.DAOS_PROP_PO_SVC_REDUN_FAC_DEFAULT
)

const (
	// DaosMdCapEnv is the name of the environment variable defining the size of a metadata pmem
	// pool/file in MiBs.
	DaosMdCapEnv = C.DAOS_MD_CAP_ENV
	// DefaultDaosMdCapSize is the default size of a metadata pmem pool/file in MiBs.
	DefaultDaosMdCapSize = C.DEFAULT_DAOS_MD_CAP_SIZE
)

// LabelIsValid checks a label to verify that it meets length/content
// requirements.
func LabelIsValid(label string) bool {
	cLabel := C.CString(label)
	defer C.free(unsafe.Pointer(cLabel))

	return bool(C.daos_label_is_valid(cLabel))
}

// EcCellSizeIsValid checks EC cell Size to verify that it meets size
// requirements.
func EcCellSizeIsValid(sz uint64) bool {
	if sz > math.MaxUint32 {
		return false
	}
	return bool(C.daos_ec_cs_valid(C.uint32_t(sz)))
}

// EcPdaIsValid checks EC performance domain affinity level that it
// doesn't exceed max unsigned int 32 bits.
func EcPdaIsValid(pda uint64) bool {
	if pda > math.MaxUint32 {
		return false
	}
	return bool(C.daos_ec_pda_valid(C.uint32_t(pda)))
}

// RpPdaIsValid checks RP performance domain affinity level that it
// doesn't exceed max unsigned int 32 bits.
func RpPdaIsValid(pda uint64) bool {
	if pda > math.MaxUint32 {
		return false
	}
	return bool(C.daos_rp_pda_valid(C.uint32_t(pda)))
}

// PoolPolicy defines a type to be used to represent DAOS pool policies.
type PoolPolicy uint32

const (
	// PoolPolicyIoSize sets the pool's policy to io_size
	PoolPolicyIoSize PoolPolicy = C.DAOS_MEDIA_POLICY_IO_SIZE
	// PoolPolicyWriteIntensivity sets the pool's policy to write_intensivity
	PoolPolicyWriteIntensivity PoolPolicy = C.DAOS_MEDIA_POLICY_WRITE_INTENSIVITY
)

// PoolPolicyIsValid returns a boolean indicating whether or not the
// pool tiering policy string is valid.
func PoolPolicyIsValid(polStr string) bool {
	var polDesc C.struct_policy_desc_t

	cStr := C.CString(polStr)
	defer C.free(unsafe.Pointer(cStr))

	return bool(C.daos_policy_try_parse(cStr, &polDesc))
}

const (
	PoolScrubModeOff   = C.DAOS_SCRUB_MODE_OFF
	PoolScrubModeLazy  = C.DAOS_SCRUB_MODE_LAZY
	PoolScrubModeTimed = C.DAOS_SCRUB_MODE_TIMED
)

const (
	PoolCheckpointDisabled = C.DAOS_CHECKPOINT_DISABLED
	PoolCheckpointTimed    = C.DAOS_CHECKPOINT_TIMED
	PoolCheckpointLazy     = C.DAOS_CHECKPOINT_LAZY
)

const (
	PoolPerfDomainRoot   = C.PO_COMP_TP_ROOT
	PoolPerfDomainGrp    = C.PO_COMP_TP_GRP
	PoolPerfDomainNode   = C.PO_COMP_TP_NODE
	PoolPerfDomainRank   = C.PO_COMP_TP_RANK
	PoolPerfDomainTarget = C.PO_COMP_TP_TARGET
)

const (
	PoolReintModeDataSync   = C.DAOS_REINT_MODE_DATA_SYNC
	PoolReintModeNoDataSync = C.DAOS_REINT_MODE_NO_DATA_SYNC
)
