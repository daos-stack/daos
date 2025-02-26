//
// (C) Copyright 2021-2023 Intel Corporation.
// (C) Copyright 2025 Google LLC
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import (
	"encoding/json"
	"fmt"
	"math"
	"sort"
	"strconv"
	"strings"
	"unsafe"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

/*
#include <daos_prop.h>
#include <daos_pool.h>
#include <daos/object.h>
#include <daos/pool_map.h>
#include <daos_srv/control.h>

#cgo LDFLAGS: -ldaos_common -lgurt -lcart
*/
import "C"

const (
	// MaxLabelLength is the maximum length of a label.
	MaxLabelLength = C.DAOS_PROP_LABEL_MAX_LEN
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
	// PoolDataThreshold is the data threshold size for a pool
	PoolDataThresh = C.DAOS_PROP_PO_DATA_THRESH
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
	PoolPropertyReintMode      = C.DAOS_PROP_PO_REINT_MODE
	PoolPropertySvcOpsEnabled  = C.DAOS_PROP_PO_SVC_OPS_ENABLED
	PoolPropertySvcOpsEntryAge = C.DAOS_PROP_PO_SVC_OPS_ENTRY_AGE
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
	// PoolSelfHealingDelayRebuild sets the self-healing strategy to delay-rebuild.
	PoolSelfHealingDelayRebuild = C.DAOS_SELF_HEAL_DELAY_REBUILD
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
	PoolSvcOpsEntryAgeMin  = C.DAOS_PROP_PO_SVC_OPS_ENTRY_AGE_MIN
	PoolSvcOpsEntryAgeMax  = C.DAOS_PROP_PO_SVC_OPS_ENTRY_AGE_MAX
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

// DataThreshIsValid verifies that the input value meets the required criteria.
func DataThreshIsValid(size uint64) bool {
	if size > math.MaxUint32 {
		return false
	}
	return bool(C.daos_data_thresh_valid(C.uint32_t(size)))
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
	PoolPerfDomainRoot        = C.PO_COMP_TP_ROOT
	PoolPerfDomainUserDefined = C.PO_COMP_TP_PERF
)

const (
	PoolReintModeDataSync    = C.DAOS_REINT_MODE_DATA_SYNC
	PoolReintModeNoDataSync  = C.DAOS_REINT_MODE_NO_DATA_SYNC
	PoolReintModeIncremental = C.DAOS_REINT_MODE_INCREMENTAL
)

func numericMarshaler(v *PoolPropertyValue) ([]byte, error) {
	n, err := v.GetNumber()
	if err != nil {
		return nil, err
	}
	return json.Marshal(n)
}

// PoolProperties returns a map of property names to handlers
// for processing property values.
func PoolProperties() PoolPropertyMap {
	return map[string]*PoolPropHandler{
		"reclaim": {
			Property: PoolProperty{
				Number:      PoolPropertySpaceReclaim,
				Description: "Reclaim strategy",
			},
			values: map[string]uint64{
				"disabled": PoolSpaceReclaimDisabled,
				"lazy":     PoolSpaceReclaimLazy,
				"time":     PoolSpaceReclaimTime,
			},
		},
		"self_heal": {
			Property: PoolProperty{
				Number:      PoolPropertySelfHealing,
				Description: "Self-healing policy",
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					switch n {
					case PoolSelfHealingAutoExclude:
						return "exclude"
					case PoolSelfHealingAutoRebuild:
						return "rebuild"
					case PoolSelfHealingDelayRebuild:
						return "delay_rebuild"
					case PoolSelfHealingAutoExclude | PoolSelfHealingAutoRebuild:
						return "exclude,rebuild"
					case PoolSelfHealingAutoExclude | PoolSelfHealingDelayRebuild:
						return "exclude,delay_rebuild"
					default:
						return "unknown"
					}
				},
			},
			values: map[string]uint64{
				"exclude":               PoolSelfHealingAutoExclude,
				"rebuild":               PoolSelfHealingAutoRebuild,
				"delay_rebuild":         PoolSelfHealingDelayRebuild,
				"exclude,rebuild":       PoolSelfHealingAutoExclude | PoolSelfHealingAutoRebuild,
				"rebuild,exclude":       PoolSelfHealingAutoExclude | PoolSelfHealingAutoRebuild,
				"delay_rebuild,exclude": PoolSelfHealingAutoExclude | PoolSelfHealingDelayRebuild,
				"exclude,delay_rebuild": PoolSelfHealingAutoExclude | PoolSelfHealingDelayRebuild,
			},
		},
		"space_rb": {
			Property: PoolProperty{
				Number:      PoolPropertyReservedSpace,
				Description: "Rebuild space ratio",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					rbErr := errors.Errorf("invalid space_rb value %s (valid values: 0-100)", s)
					rsPct, err := strconv.ParseUint(strings.ReplaceAll(s, "%", ""), 10, 64)
					if err != nil {
						return nil, rbErr
					}
					if rsPct > 100 {
						return nil, rbErr
					}
					return &PoolPropertyValue{rsPct}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return fmt.Sprintf("%d%%", n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
		"svc_ops_enabled": {
			Property: PoolProperty{
				Number:      PoolPropertySvcOpsEnabled,
				Description: "Metadata duplicate operations detection enabled",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					oeErr := errors.Errorf("invalid svc_ops_enabled value %s (valid values: 0-1)", s)
					oeVal, err := strconv.ParseUint(s, 10, 32)
					if err != nil {
						return nil, oeErr
					}
					if oeVal > 1 {
						return nil, errors.Wrap(oeErr, "value supplied is greater than 1")
					}
					return &PoolPropertyValue{oeVal}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return fmt.Sprintf("%d", n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
		"svc_ops_entry_age": {
			Property: PoolProperty{
				Number:      PoolPropertySvcOpsEntryAge,
				Description: "Metadata duplicate operations KVS max entry age, in seconds",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					oeErr := errors.Errorf("invalid svc_ops_entry_age %s (valid values: %d-%d)", s, PoolSvcOpsEntryAgeMin, PoolSvcOpsEntryAgeMax)
					oeVal, err := strconv.ParseUint(s, 10, 32)
					if err != nil {
						return nil, oeErr
					}
					if oeVal < PoolSvcOpsEntryAgeMin || oeVal > PoolSvcOpsEntryAgeMax {
						return nil, errors.Wrap(oeErr, "value supplied is out of range")
					}
					return &PoolPropertyValue{oeVal}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return fmt.Sprintf("%d", n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
		"label": {
			Property: PoolProperty{
				Number:      PoolPropertyLabel,
				Description: "Pool label",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					if !LabelIsValid(s) {
						return nil, errors.Errorf("invalid label %q", s)
					}
					return &PoolPropertyValue{s}, nil
				},
			},
		},
		"ec_cell_sz": {
			Property: PoolProperty{
				Number:      PoolPropertyECCellSize,
				Description: "EC cell size",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					b, err := humanize.ParseBytes(s)
					if err != nil || !EcCellSizeIsValid(b) {
						return nil, errors.Errorf("invalid EC Cell size %q", s)
					}

					return &PoolPropertyValue{b}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return humanize.IBytes(n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
		"rd_fac": {
			Property: PoolProperty{
				Number:      PoolPropertyRedunFac,
				Description: "Pool redundancy factor",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					rbErr := errors.Errorf("invalid redun fac value %s (valid values: 0-%d)", s, PoolRedunFacMax)
					rfVal, err := strconv.ParseUint(s, 10, 64)
					if err != nil {
						return nil, rbErr
					}
					if rfVal > PoolRedunFacMax {
						return nil, rbErr
					}
					return &PoolPropertyValue{rfVal}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return fmt.Sprintf("%d", n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
		"ec_pda": {
			Property: PoolProperty{
				Number:      PoolPropertyECPda,
				Description: "Performance domain affinity level of EC",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					ecpdaErr := errors.Errorf("invalid ec_pda value %q", s)
					pdalvl, err := strconv.ParseUint(s, 10, 32)
					if err != nil || !EcPdaIsValid(pdalvl) {
						return nil, ecpdaErr
					}
					return &PoolPropertyValue{pdalvl}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return fmt.Sprintf("%d", n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
		"rp_pda": {
			Property: PoolProperty{
				Number:      PoolPropertyRPPda,
				Description: "Performance domain affinity level of RP",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					rppdaErr := errors.Errorf("invalid rp_pda value %q", s)
					pdalvl, err := strconv.ParseUint(s, 10, 32)
					if err != nil || !RpPdaIsValid(pdalvl) {
						return nil, rppdaErr
					}
					return &PoolPropertyValue{pdalvl}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return fmt.Sprintf("%d", n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
		"data_thresh": {
			Property: PoolProperty{
				Number:      PoolDataThresh,
				Description: "Data bdev threshold size",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					b, err := humanize.ParseBytes(s)
					if err != nil || !DataThreshIsValid(b) {
						return nil, errors.Errorf("invalid data threshold size %q", s)
					}

					return &PoolPropertyValue{b}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return humanize.IBytes(n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
		"global_version": {
			Property: PoolProperty{
				Number:      PoolPropertyGlobalVersion,
				Description: "Global Version",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					gvErr := errors.Errorf("invalid global version %q", s)
					gvvl, err := strconv.ParseUint(s, 10, 32)
					if err != nil {
						return nil, gvErr
					}
					return &PoolPropertyValue{gvvl}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return fmt.Sprintf("%d", n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
		"upgrade_status": {
			Property: PoolProperty{
				Number:      PoolPropertyUpgradeStatus,
				Description: "Upgrade Status",
			},
			values: map[string]uint64{
				"not started": PoolUpgradeStatusNotStarted,
				"in progress": PoolUpgradeStatusInProgress,
				"completed":   PoolUpgradeStatusCompleted,
				"failed":      PoolUpgradeStatusFailed,
			},
		},
		"scrub": {
			Property: PoolProperty{
				Number:      PoolPropertyScrubMode,
				Description: "Checksum scrubbing mode",
			},
			values: map[string]uint64{
				"off":   PoolScrubModeOff,
				"lazy":  PoolScrubModeLazy,
				"timed": PoolScrubModeTimed,
			},
		},
		"scrub_freq": {
			Property: PoolProperty{
				Number:      PoolPropertyScrubFreq,
				Description: "Checksum scrubbing frequency",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					rbErr := errors.Errorf("invalid Scrubbing Frequency value %s", s)
					rsPct, err := strconv.ParseUint(strings.ReplaceAll(s, "%", ""), 10, 64)
					if err != nil {
						return nil, rbErr
					}
					return &PoolPropertyValue{rsPct}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return fmt.Sprintf("%d", n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
		"scrub_thresh": {
			Property: PoolProperty{
				Number:      PoolPropertyScrubThresh,
				Description: "Checksum scrubbing threshold",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					rbErr := errors.Errorf("invalid Scrubbing Threshold value %q", s)
					rsPct, err := strconv.ParseUint(strings.ReplaceAll(s, "%", ""), 10, 64)
					if err != nil {
						return nil, rbErr
					}
					return &PoolPropertyValue{rsPct}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return fmt.Sprintf("%d", n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
		"perf_domain": {
			Property: PoolProperty{
				Number:      PoolPropertyPerfDomain,
				Description: "Pool performance domain",
			},
			values: map[string]uint64{
				"root":  PoolPerfDomainRoot,
				"group": PoolPerfDomainUserDefined,
			},
		},
		"svc_rf": {
			Property: PoolProperty{
				Number:      PoolPropertySvcRedunFac,
				Description: "Pool service redundancy factor",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					svcRFErr := errors.Errorf("invalid service redundancy factor value %s (valid values: 0-%d)", s, PoolSvcRedunFacMax)
					svcRFVal, err := strconv.ParseUint(s, 10, 64)
					if err != nil {
						return nil, svcRFErr
					}
					if svcRFVal > PoolSvcRedunFacMax {
						return nil, svcRFErr
					}
					return &PoolPropertyValue{svcRFVal}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return fmt.Sprintf("%d", n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
		"svc_list": {
			Property: PoolProperty{
				Number:      PoolPropertySvcList,
				Description: "Pool service replica list",
				valueHandler: func(string) (*PoolPropertyValue, error) {
					return nil, errors.New("cannot set pool service replica list")
				},
				valueStringer: func(v *PoolPropertyValue) string {
					return v.String()
				},
				valueMarshaler: func(v *PoolPropertyValue) ([]byte, error) {
					rs, err := ranklist.CreateRankSet(v.String())
					if err != nil {
						return nil, err
					}
					return json.Marshal(rs.Ranks())
				},
			},
		},
		"checkpoint": {
			Property: PoolProperty{
				Number:      PoolPropertyCheckpointMode,
				Description: "WAL checkpointing behavior",
			},
			values: map[string]uint64{
				"disabled": PoolCheckpointDisabled,
				"timed":    PoolCheckpointTimed,
				"lazy":     PoolCheckpointLazy,
			},
		},
		"checkpoint_freq": {
			Property: PoolProperty{
				Number:      PoolPropertyCheckpointFreq,
				Description: "WAL checkpointing frequency, in seconds",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					rbErr := errors.Errorf("invalid Checkpointing Frequency value %s", s)
					rsPct, err := strconv.ParseUint(s, 10, 64)
					if err != nil {
						return nil, rbErr
					}
					return &PoolPropertyValue{rsPct}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return fmt.Sprintf("%d", n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
		"checkpoint_thresh": {
			Property: PoolProperty{
				Number:      PoolPropertyCheckpointThresh,
				Description: "WAL checkpoint threshold, in percentage",
				valueHandler: func(s string) (*PoolPropertyValue, error) {
					rbErr := errors.Errorf("invalid Checkpointing threshold value %s", s)
					rsPct, err := strconv.ParseUint(s, 10, 32)
					if err != nil {
						return nil, rbErr
					}
					return &PoolPropertyValue{rsPct}, nil
				},
				valueStringer: func(v *PoolPropertyValue) string {
					n, err := v.GetNumber()
					if err != nil {
						return "not set"
					}
					return fmt.Sprintf("%d", n)
				},
				valueMarshaler: numericMarshaler,
			},
		},
		"reintegration": {
			Property: PoolProperty{
				Number:      PoolPropertyReintMode,
				Description: "Reintegration mode",
			},
			values: map[string]uint64{
				"data_sync":    PoolReintModeDataSync,
				"no_data_sync": PoolReintModeNoDataSync,
				"incremental":  PoolReintModeIncremental,
			},
		},
	}
}

func PoolDeprecatedProperties() map[string]string {
	return map[string]string{
		"rf": "rd_fac",
	}
}

// GetProperty returns a *PoolProperty for the property name, if valid.
func (m PoolPropertyMap) GetProperty(name string) (*PoolProperty, error) {
	h, found := m[name]
	if !found {
		return nil, errors.Errorf("unknown property %q", name)
	}
	return h.GetProperty(name), nil
}

// PoolPropertyValue encapsulates the logic for storing a string or uint64
// property value.
type PoolPropertyValue struct {
	data interface{}
}

// SetString sets the property value to a string.
func (ppv *PoolPropertyValue) SetString(strVal string) {
	ppv.data = strVal
}

// SetNumber sets the property value to a number.
func (ppv *PoolPropertyValue) SetNumber(numVal uint64) {
	ppv.data = numVal
}

func (ppv *PoolPropertyValue) IsSet() bool {
	return ppv != nil && ppv.data != nil
}

func (ppv *PoolPropertyValue) String() string {
	if !ppv.IsSet() {
		return "value not set"
	}

	switch v := ppv.data.(type) {
	case string:
		return v
	case uint64:
		return strconv.FormatUint(v, 10)
	default:
		return fmt.Sprintf("unknown data type for %+v", ppv.data)
	}
}

// GetNumber returns the numeric value set for the property,
// or an error if the value is not a number.
func (ppv *PoolPropertyValue) GetNumber() (uint64, error) {
	if !ppv.IsSet() {
		return 0, errors.New("value not set")
	}
	if v, ok := ppv.data.(uint64); ok {
		return v, nil
	}
	return 0, errors.Errorf("%+v is not uint64", ppv.data)
}

// PoolProperty contains a name/value pair representing a pool property.
type PoolProperty struct {
	Number         uint32            `json:"-"`
	Name           string            `json:"name"`
	Description    string            `json:"description"`
	Value          PoolPropertyValue `json:"value"`
	valueHandler   func(string) (*PoolPropertyValue, error)
	valueStringer  func(*PoolPropertyValue) string
	valueMarshaler func(*PoolPropertyValue) ([]byte, error)
}

func (p *PoolProperty) SetValue(strVal string) error {
	if p.valueHandler == nil {
		p.Value.data = strVal
		return nil
	}
	v, err := p.valueHandler(strVal)
	if err != nil {
		return err
	}
	p.Value = *v
	return nil
}

func (p *PoolProperty) String() string {
	if p == nil {
		return "<nil>"
	}

	return p.Name + ":" + p.StringValue()
}

func (p *PoolProperty) StringValue() string {
	if p == nil {
		return "<nil>"
	}
	if p.valueStringer != nil {
		return p.valueStringer(&p.Value)
	}
	return p.Value.String()
}

func (p *PoolProperty) MarshalJSON() (_ []byte, err error) {
	if p == nil {
		return nil, errors.New("nil property")
	}

	var value json.RawMessage
	if p.valueMarshaler != nil {
		if value, err = p.valueMarshaler(&p.Value); err != nil {
			return nil, err
		}
	} else {
		if value, err = json.Marshal(p.StringValue()); err != nil {
			return nil, err
		}
	}

	type toJSON PoolProperty
	return json.Marshal(&struct {
		*toJSON
		Value json.RawMessage `json:"value"`
	}{
		Value:  value,
		toJSON: (*toJSON)(p),
	})
}

type valueMap map[string]uint64

func (m valueMap) Keys() []string {
	keys := make([]string, 0, len(m))
	for key := range m {
		keys = append(keys, key)
	}
	sort.Strings(keys)

	return keys
}

type PoolPropHandler struct {
	Property PoolProperty
	values   valueMap
}

func (pph *PoolPropHandler) Values() []string {
	return pph.values.Keys()
}

func (pph *PoolPropHandler) GetProperty(name string) *PoolProperty {
	if pph.Property.valueHandler == nil {
		pph.Property.valueHandler = func(in string) (*PoolPropertyValue, error) {
			if val, found := pph.values[strings.ToLower(in)]; found {
				return &PoolPropertyValue{val}, nil
			}
			return nil, errors.Errorf("invalid value %q for %s (valid: %s)", in, name, strings.Join(pph.Values(), ","))
		}
	}

	if pph.Property.valueStringer == nil {
		valNameMap := make(map[uint64]string)
		for name, number := range pph.values {
			valNameMap[number] = name
		}

		pph.Property.valueStringer = func(v *PoolPropertyValue) string {
			n, err := v.GetNumber()
			if err == nil {
				if name, found := valNameMap[n]; found {
					return name
				}
			}
			return v.String()
		}
	}

	pph.Property.Name = name
	return &pph.Property
}

type PoolPropertyMap map[string]*PoolPropHandler

func (m PoolPropertyMap) Keys() []string {
	keys := make([]string, 0, len(m))
	for key := range m {
		keys = append(keys, key)
	}
	sort.Strings(keys)

	return keys
}
