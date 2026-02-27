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
	"fmt"
	"strings"
	"unsafe"

	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
)

//export daos_control_storage_device_list
func daos_control_storage_device_list(
	handle C.uintptr_t,
	ndisks *C.int,
	devices *C.struct_device_list,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	req := &control.SmdQueryReq{
		OmitPools: true,
	}

	resp, err := control.SmdQuery(ctx.ctx(), ctx.client, req)
	if err != nil {
		return C.int(errorToRC(err))
	}

	// Count total devices across all hosts
	totalDevices := 0
	for _, hss := range resp.HostStorage {
		if hss.HostStorage != nil && hss.HostStorage.SmdInfo != nil {
			totalDevices += len(hss.HostStorage.SmdInfo.Devices)
		}
	}

	if ndisks != nil {
		*ndisks = C.int(totalDevices)
	}

	// If devices is nil, caller just wants the count
	if devices == nil {
		return 0
	}

	// Fill in device information. Zero-initialize the output array to ensure
	// partially-filled arrays (tgtidx) and conditionally-set fields (device_id)
	// don't contain uninitialized memory.
	C.memset(unsafe.Pointer(devices), 0,
		C.size_t(totalDevices)*C.size_t(unsafe.Sizeof(*devices)))
	deviceSlice := unsafe.Slice(devices, totalDevices)
	idx := 0
	for _, hss := range resp.HostStorage {
		if hss.HostStorage == nil || hss.HostStorage.SmdInfo == nil {
			continue
		}

		// Get hostname from host set
		hostAddr := hss.HostSet.RangedString()
		// Strip port if present (format may be "host:port")
		if colonIdx := strings.LastIndex(hostAddr, ":"); colonIdx > 0 {
			hostAddr = hostAddr[:colonIdx]
		}

		for _, dev := range hss.HostStorage.SmdInfo.Devices {
			if idx >= totalDevices {
				break
			}

			// Copy device UUID
			if devUUID, err := uuid.Parse(dev.UUID); err == nil {
				copyUUIDToC(devUUID, &deviceSlice[idx].device_id)
			}

			// Copy state (truncate if needed)
			state := dev.Ctrlr.NvmeState.String()
			copyStringToCharArray(state, &deviceSlice[idx].state[0], 10)

			// Copy rank
			deviceSlice[idx].rank = C.int(dev.Rank)

			// Copy hostname
			copyStringToCharArray(hostAddr, &deviceSlice[idx].host[0], C.DSS_HOSTNAME_MAX_LEN)

			// Copy target IDs
			nTgts := len(dev.TargetIDs)
			if nTgts > C.MAX_TEST_TARGETS_PER_DEVICE {
				nTgts = C.MAX_TEST_TARGETS_PER_DEVICE
			}
			for i := 0; i < nTgts; i++ {
				deviceSlice[idx].tgtidx[i] = C.int(dev.TargetIDs[i])
			}
			deviceSlice[idx].n_tgtidx = C.int(nTgts)

			idx++
		}
	}

	return 0
}

//export daos_control_storage_set_nvme_fault
func daos_control_storage_set_nvme_fault(
	handle C.uintptr_t,
	host *C.char,
	devUUID *C.uuid_t,
	force C.int,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	goUUID := uuidFromC(devUUID)

	req := &control.SmdManageReq{
		IDs:       goUUID.String(),
		Operation: control.SetFaultyOp,
	}

	// Set host list if provided
	if host != nil {
		hostStr := goString(host)
		if hostStr != "" {
			req.SetHostList([]string{hostStr})
		}
	}

	resp, err := control.SmdManage(ctx.ctx(), ctx.client, req)
	if err != nil {
		return C.int(errorToRC(err))
	}

	// Check for host errors
	if resp.HostErrorsResp.GetHostErrors().ErrorCount() > 0 {
		for _, hes := range resp.HostErrorsResp.GetHostErrors() {
			return C.int(errorToRC(hes.HostError))
		}
	}

	return 0
}

//export daos_control_storage_query_device_health
func daos_control_storage_query_device_health(
	handle C.uintptr_t,
	host *C.char,
	statsKey *C.char,
	statsOut *C.char,
	statsOutLen C.int,
	devUUID *C.uuid_t,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	goUUID := uuidFromC(devUUID)

	req := &control.SmdQueryReq{
		OmitPools:        true,
		IncludeBioHealth: true,
		UUID:             goUUID.String(),
	}

	// Set host list if provided
	if host != nil {
		hostStr := goString(host)
		if hostStr != "" {
			req.SetHostList([]string{hostStr})
		}
	}

	resp, err := control.SmdQuery(ctx.ctx(), ctx.client, req)
	if err != nil {
		return C.int(errorToRC(err))
	}

	// Find the device and extract the requested health stat
	key := goString(statsKey)
	for _, hss := range resp.HostStorage {
		if hss.HostStorage == nil || hss.HostStorage.SmdInfo == nil {
			continue
		}
		for _, dev := range hss.HostStorage.SmdInfo.Devices {
			if dev.Ctrlr.HealthStats == nil {
				continue
			}

			// Get the health stat value based on key
			var value string
			stats := dev.Ctrlr.HealthStats
			switch key {
			case "temperature":
				value = fmt.Sprintf("%d", stats.Temperature)
			case "media_errs":
				value = fmt.Sprintf("%d", stats.MediaErrors)
			case "bio_read_errs":
				value = fmt.Sprintf("%d", stats.ReadErrors)
			case "bio_write_errs":
				value = fmt.Sprintf("%d", stats.WriteErrors)
			case "bio_unmap_errs":
				value = fmt.Sprintf("%d", stats.UnmapErrors)
			case "checksum_errs":
				value = fmt.Sprintf("%d", stats.ChecksumErrors)
			case "power_cycles":
				value = fmt.Sprintf("%d", stats.PowerCycles)
			case "unsafe_shutdowns":
				value = fmt.Sprintf("%d", stats.UnsafeShutdowns)
			default:
				// Unknown key, return empty
				value = ""
			}

			if value != "" && statsOut != nil && statsOutLen > 0 {
				copyStringToCharArray(value, statsOut, int(statsOutLen))
			}
			return 0
		}
	}

	return C.int(daos.Nonexistent)
}

// Helper function to copy a Go string to a C char array
func copyStringToCharArray(s string, dest *C.char, maxLen int) {
	if dest == nil || maxLen <= 0 {
		return
	}

	// Create a slice backed by the C array
	destSlice := unsafe.Slice(dest, maxLen)

	// Copy string bytes
	copyLen := len(s)
	if copyLen >= maxLen {
		copyLen = maxLen - 1
	}
	for i := 0; i < copyLen; i++ {
		destSlice[i] = C.char(s[i])
	}
	destSlice[copyLen] = 0 // null terminate
}
