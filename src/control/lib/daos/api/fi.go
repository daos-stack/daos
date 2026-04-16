//
// (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package api

/*
#include <stdint.h>
#include <daos/common.h>
*/
import "C"

import "fmt"

var (
	// failLocMap maps from strings to DAOS fault injection location constants.
	// The definitions come from daos_common.h.
	// TODO: Add the rest of existing fault locs. Maybe auto-generate this mapping?
	failLocMap = map[string]C.uint64_t{
		"DAOS_CHK_CONT_ORPHAN":         C.DAOS_CHK_CONT_ORPHAN,
		"DAOS_CHK_CONT_BAD_LABEL":      C.DAOS_CHK_CONT_BAD_LABEL,
		"DAOS_CHK_LEADER_BLOCK":        C.DAOS_CHK_LEADER_BLOCK,
		"DAOS_CHK_LEADER_FAIL_REGPOOL": C.DAOS_CHK_LEADER_FAIL_REGPOOL,
		"DAOS_CHK_PS_NOTIFY_LEADER":    C.DAOS_CHK_PS_NOTIFY_LEADER,
		"DAOS_CHK_PS_NOTIFY_ENGINE":    C.DAOS_CHK_PS_NOTIFY_ENGINE,
		"DAOS_CHK_SYNC_ORPHAN_PROCESS": C.DAOS_CHK_SYNC_ORPHAN_PROCESS,
		"DAOS_CHK_FAIL_REPORT_POOL1":   C.DAOS_CHK_FAIL_REPORT_POOL1,
		"DAOS_CHK_FAIL_REPORT_POOL2":   C.DAOS_CHK_FAIL_REPORT_POOL2,
		"DAOS_CHK_ENGINE_DEATH":        C.DAOS_CHK_ENGINE_DEATH,
		"DAOS_CHK_VERIFY_CONT_SHARDS":  C.DAOS_CHK_VERIFY_CONT_SHARDS,
		"DAOS_CHK_ORPHAN_POOL_SHARD":   C.DAOS_CHK_ORPHAN_POOL_SHARD,
	}
)

// FaultLocationFromString converts a string to a fault injection location value.
func FaultLocationFromString(str string) (uint64, error) {
	loc, found := failLocMap[str]
	if !found {
		return 0, fmt.Errorf("invalid fault injection location %q", str)
	}
	return uint64(loc), nil
}
