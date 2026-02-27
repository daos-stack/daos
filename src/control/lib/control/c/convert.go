//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

/*
#include <stdlib.h>
#include <uuid/uuid.h>
#include <daos_types.h>
#include <daos_prop.h>

#include "daos_control_util.h"
*/
import "C"
import (
	"os/user"
	"strconv"
	"unsafe"

	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

// uuidFromC converts a C uuid_t to a Go uuid.UUID.
func uuidFromC(cUUID *C.uuid_t) uuid.UUID {
	if cUUID == nil {
		return uuid.Nil
	}
	var goUUID uuid.UUID
	for i := 0; i < 16; i++ {
		goUUID[i] = byte(cUUID[i])
	}
	return goUUID
}

// poolIDFromC converts a C uuid_t to a pool ID string.
func poolIDFromC(cUUID *C.uuid_t) string {
	return uuidFromC(cUUID).String()
}

// copyUUIDToC copies a Go uuid.UUID to a C uuid_t.
func copyUUIDToC(goUUID uuid.UUID, cUUID *C.uuid_t) {
	if cUUID == nil {
		return
	}
	for i := 0; i < 16; i++ {
		cUUID[i] = C.uchar(goUUID[i])
	}
}

// rankListFromC converts a C d_rank_list_t to a slice of ranklist.Rank.
func rankListFromC(cRankList *C.d_rank_list_t) []ranklist.Rank {
	if cRankList == nil || cRankList.rl_nr == 0 || cRankList.rl_ranks == nil {
		return nil
	}

	ranks := make([]ranklist.Rank, cRankList.rl_nr)
	cRanks := unsafe.Slice(cRankList.rl_ranks, cRankList.rl_nr)
	for i, r := range cRanks {
		ranks[i] = ranklist.Rank(r)
	}
	return ranks
}

// copyRankListToC copies a slice of ranklist.Rank to a pre-allocated C d_rank_list_t.
// The destination's rl_nr is used as the capacity; only that many ranks will be copied.
func copyRankListToC(ranks []ranklist.Rank, cRankList *C.d_rank_list_t) {
	if cRankList == nil || len(ranks) == 0 {
		if cRankList != nil {
			cRankList.rl_nr = 0
		}
		return
	}

	// Verify rl_ranks is valid before attempting to write
	if cRankList.rl_ranks == nil {
		cRankList.rl_nr = 0
		return
	}

	// Use destination's rl_nr as capacity - don't write more than it can hold
	toCopy := len(ranks)
	if int(cRankList.rl_nr) < toCopy {
		toCopy = int(cRankList.rl_nr)
	}

	// Don't write anything if there's nothing to copy
	if toCopy == 0 {
		cRankList.rl_nr = 0
		return
	}

	cRankList.rl_nr = C.uint32_t(toCopy)
	cRanks := unsafe.Slice(cRankList.rl_ranks, toCopy)
	for i := 0; i < toCopy; i++ {
		cRanks[i] = C.d_rank_t(ranks[i])
	}
}

// goString safely converts a C string to a Go string, returning empty string for nil.
func goString(cStr *C.char) string {
	if cStr == nil {
		return ""
	}
	return C.GoString(cStr)
}

// uidToUsername converts a numeric UID to a username string.
func uidToUsername(uid uint32) string {
	u, err := user.LookupId(strconv.FormatUint(uint64(uid), 10))
	if err != nil {
		return ""
	}
	return u.Username
}

// gidToGroupname converts a numeric GID to a group name string.
func gidToGroupname(gid uint32) string {
	g, err := user.LookupGroupId(strconv.FormatUint(uint64(gid), 10))
	if err != nil {
		return ""
	}
	return g.Name
}

// propsFromC converts a C daos_prop_t to Go pool properties.
// This extracts properties that are relevant for pool creation.
func propsFromC(cProps *C.daos_prop_t) []*daos.PoolProperty {
	if cProps == nil || cProps.dpp_nr == 0 {
		return nil
	}

	entries := unsafe.Slice(cProps.dpp_entries, cProps.dpp_nr)
	allProps := daos.PoolProperties()
	var props []*daos.PoolProperty

	for i := range entries {
		entry := &entries[i]
		propType := uint32(entry.dpe_type)

		// Find the property name for this type
		var propName string
		for name, handler := range allProps {
			if handler.GetProperty(name).Number == propType {
				propName = name
				break
			}
		}
		if propName == "" {
			continue // Unknown property type
		}

		prop := allProps[propName].GetProperty(propName)

		// Extract value based on property type
		switch propType {
		case C.DAOS_PROP_PO_LABEL:
			// String property - use helper function to access union
			strPtr := C.get_dpe_str(entry)
			if strPtr != nil {
				prop.Value.SetString(C.GoString(strPtr))
			}
		default:
			// Numeric property - use helper function to access union
			prop.Value.SetNumber(uint64(C.get_dpe_val(entry)))
		}

		props = append(props, prop)
	}

	return props
}
