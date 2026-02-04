//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

/*
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <uuid/uuid.h>
#include <daos_types.h>

#include <daos/control_types.h>
*/
import "C"
import (
	"encoding/json"
	"strings"
	"unsafe"

	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
)

// ndisks is in/out: on input it is the caller's allocated capacity of the
// devices array; on output it is the number of devices populated (or, if the
// caller's buffer was too small, the required capacity). Passing devices=NULL
// queries just the count (ndisks must be non-NULL). Passing devices!=NULL
// with *ndisks smaller than the required count returns BufTooSmall and sets
// *ndisks to the required capacity; the caller can grow and retry.
//
//export daos_control_storage_device_list
func daos_control_storage_device_list(handle C.uintptr_t, ndisks *C.int, devices *C.struct_device_list) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		if ndisks == nil {
			return daos.InvalidInput
		}

		resp, err := control.SmdQuery(ctx.ctx(), ctx.client, &control.SmdQueryReq{OmitPools: true})
		if err != nil {
			return err
		}

		totalDevices := 0
		for _, hss := range resp.HostStorage {
			if hss.HostStorage != nil && hss.HostStorage.SmdInfo != nil {
				totalDevices += len(hss.HostStorage.SmdInfo.Devices)
			}
		}

		// Query-only mode.
		if devices == nil {
			*ndisks = C.int(totalDevices)
			return nil
		}

		// Write the required count either way so the caller can grow the buffer
		// and retry on BufTooSmall, or so an exact-fit caller sees *ndisks ==
		// populated count.
		capacity := int(*ndisks)
		if capacity < totalDevices {
			*ndisks = C.int(totalDevices)
			return daos.BufTooSmall
		}

		// Zero-init the region we'll populate so partially-filled tgtidx and
		// conditionally-set device_id fields aren't uninitialized. Bound the
		// memset by the populated count, not the caller's capacity.
		if totalDevices > 0 {
			C.memset(unsafe.Pointer(devices), 0,
				C.size_t(totalDevices)*C.size_t(unsafe.Sizeof(*devices)))
		}
		deviceSlice := unsafe.Slice(devices, capacity)
		idx := 0
		for _, hss := range resp.HostStorage {
			if hss.HostStorage == nil || hss.HostStorage.SmdInfo == nil {
				continue
			}

			// Strip port from "host:port" if present.
			hostAddr := hss.HostSet.RangedString()
			if colonIdx := strings.LastIndex(hostAddr, ":"); colonIdx > 0 {
				hostAddr = hostAddr[:colonIdx]
			}

			for _, dev := range hss.HostStorage.SmdInfo.Devices {
				if idx >= totalDevices {
					break
				}

				if devUUID, err := uuid.Parse(dev.UUID); err == nil {
					copyUUIDToC(devUUID, &deviceSlice[idx].device_id)
				}

				state := dev.Ctrlr.NvmeState.String()
				copyStringToCharArray(state, &deviceSlice[idx].state[0], C.DAOS_DEV_STATE_MAX_LEN)

				deviceSlice[idx].rank = C.int(dev.Rank)

				copyStringToCharArray(hostAddr, &deviceSlice[idx].host[0], C.DAOS_HOSTNAME_MAX_LEN)

				nTgts := len(dev.TargetIDs)
				if nTgts > C.DAOS_MAX_TARGETS_PER_DEVICE {
					nTgts = C.DAOS_MAX_TARGETS_PER_DEVICE
				}
				for i := 0; i < nTgts; i++ {
					deviceSlice[idx].tgtidx[i] = C.int(dev.TargetIDs[i])
				}
				deviceSlice[idx].n_tgtidx = C.int(nTgts)

				idx++
			}
		}

		*ndisks = C.int(idx)
		return nil
	})
}

// The force parameter is kept for signature compatibility with the legacy
// dmg-based helper but is not consumed: dmg's --force only skips an
// interactive consent prompt, which does not exist in a programmatic API.
//
//export daos_control_storage_set_nvme_fault
func daos_control_storage_set_nvme_fault(handle C.uintptr_t, host *C.char, devUUID *C.uuid_t, _ C.int) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		req := &control.SmdManageReq{
			IDs:       uuidFromC(devUUID).String(),
			Operation: control.SetFaultyOp,
		}
		if hostStr := goString(host); hostStr != "" {
			req.SetHostList([]string{hostStr})
		}

		resp, err := control.SmdManage(ctx.ctx(), ctx.client, req)
		if err != nil {
			return err
		}

		// Log the full host-error set before collapsing to a single rc.
		hem := resp.HostErrorsResp.GetHostErrors()
		if hem.ErrorCount() > 0 {
			logAllHostErrors(ctx, hem)
			return firstHostStatus(hem)
		}
		return nil
	})
}

// daos_control_storage_query_device_health writes a single named health stat
// for an NVMe device into the caller's buffer.
//
// statsKey names the JSON field (e.g. "temperature"); statsOut is a
// statsOutLen-byte buffer that receives the NUL-terminated value. statsKey
// and statsOut MUST NOT alias: the key is copied to Go before the output is
// written, so aliased callers see their input clobbered.
//
//export daos_control_storage_query_device_health
func daos_control_storage_query_device_health(handle C.uintptr_t, host, statsKey, statsOut *C.char, statsOutLen C.int, devUUID *C.uuid_t) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		// Copy the key before any write to statsOut so aliasing is safe — see
		// the aliasing note in the godoc above.
		key := goString(statsKey)

		req := &control.SmdQueryReq{
			OmitPools:        true,
			IncludeBioHealth: true,
			UUID:             uuidFromC(devUUID).String(),
		}
		if hostStr := goString(host); hostStr != "" {
			req.SetHostList([]string{hostStr})
		}

		resp, err := control.SmdQuery(ctx.ctx(), ctx.client, req)
		if err != nil {
			return err
		}

		// Round-trip HealthStats through JSON to look up the key by name, matching
		// the legacy dmg-helper behavior and avoiding a per-field switch here.
		for _, hss := range resp.HostStorage {
			if hss.HostStorage == nil || hss.HostStorage.SmdInfo == nil {
				continue
			}
			for _, dev := range hss.HostStorage.SmdInfo.Devices {
				if dev.Ctrlr.HealthStats == nil {
					continue
				}
				value, err := extractHealthStat(dev.Ctrlr.HealthStats, key)
				if err != nil {
					ctx.log.Errorf("health stat lookup for %q failed: %v", key, err)
					return err
				}
				if value == "" {
					return daos.Nonexistent
				}
				if statsOut != nil && statsOutLen > 0 {
					copyStringToCharArray(value, statsOut, int(statsOutLen))
				}
				return nil
			}
		}
		return daos.Nonexistent
	})
}

// extractHealthStat marshals the health stats struct to JSON and returns the
// value for the named key. Strings are returned with their JSON quoting intact
// to match the old dmg-helper behavior (which used json_object_to_json_string).
func extractHealthStat(stats interface{}, key string) (string, error) {
	if key == "" {
		return "", nil
	}
	data, err := json.Marshal(stats)
	if err != nil {
		return "", err
	}
	var fields map[string]json.RawMessage
	if err := json.Unmarshal(data, &fields); err != nil {
		return "", err
	}
	raw, ok := fields[key]
	if !ok {
		return "", nil
	}
	return string(raw), nil
}

// copyStringToCharArray copies s into dest (length maxLen), null-terminated.
// Truncation happens on the nearest prior UTF-8 rune boundary so the result
// is always valid UTF-8.
func copyStringToCharArray(s string, dest *C.char, maxLen int) {
	if dest == nil || maxLen <= 0 {
		return
	}

	destSlice := unsafe.Slice(dest, maxLen)

	// Reserve one byte for the null terminator.
	copyLen := len(s)
	if copyLen > maxLen-1 {
		copyLen = maxLen - 1
		// Walk back past any UTF-8 continuation bytes so we don't truncate mid-rune.
		for copyLen > 0 && (s[copyLen]&0xC0) == 0x80 {
			copyLen--
		}
	}
	for i := 0; i < copyLen; i++ {
		destSlice[i] = C.char(s[i])
	}
	destSlice[copyLen] = 0
}
