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

#include <daos/control_types.h>
*/
import "C"
import (
	"strings"
	"unsafe"

	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
)

// parsePolicies parses a comma-separated "CLASS:ACTION" policy string
// into a slice of SystemCheckPolicy pointers.
func parsePolicies(policiesStr string) ([]*control.SystemCheckPolicy, error) {
	var policies []*control.SystemCheckPolicy

	for _, policyStr := range strings.Split(policiesStr, ",") {
		policyStr = strings.TrimSpace(policyStr)
		if policyStr == "" {
			continue
		}
		parts := strings.SplitN(policyStr, ":", 2)
		if len(parts) != 2 {
			return nil, daos.InvalidInput
		}
		policy, err := control.NewSystemCheckPolicy(parts[0], parts[1])
		if err != nil {
			return nil, daos.InvalidInput
		}
		policies = append(policies, policy)
	}

	return policies, nil
}

//export daos_control_check_switch
func daos_control_check_switch(
	handle C.uintptr_t,
	enable C.int,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	var err error
	if enable != 0 {
		req := &control.SystemCheckEnableReq{}
		err = control.SystemCheckEnable(ctx.ctx(), ctx.client, req)
	} else {
		req := &control.SystemCheckDisableReq{}
		err = control.SystemCheckDisable(ctx.ctx(), ctx.client, req)
	}

	return C.int(errorToRC(err))
}

//export daos_control_check_start
func daos_control_check_start(
	handle C.uintptr_t,
	flags C.uint32_t,
	poolNr C.uint32_t,
	uuids *C.uuid_t,
	policies *C.char,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	req := &control.SystemCheckStartReq{}
	req.CheckStartReq.Flags = uint32(flags)

	// Convert pool UUIDs if provided
	if uuids != nil && poolNr > 0 {
		uuidSlice := unsafe.Slice(uuids, poolNr)
		for i := uint32(0); i < uint32(poolNr); i++ {
			u := uuidFromC(&uuidSlice[i])
			if u != uuid.Nil {
				req.CheckStartReq.Uuids = append(req.CheckStartReq.Uuids, u.String())
			}
		}
	}

	// Parse policies string if provided (format: "CLASS:ACTION,CLASS:ACTION,...")
	if policies != nil {
		policiesStr := goString(policies)
		if policiesStr != "" {
			parsed, err := parsePolicies(policiesStr)
			if err != nil {
				return C.int(errorToRC(err))
			}
			req.Policies = parsed
		}
	}

	err := control.SystemCheckStart(ctx.ctx(), ctx.client, req)
	return C.int(errorToRC(err))
}

//export daos_control_check_stop
func daos_control_check_stop(
	handle C.uintptr_t,
	poolNr C.uint32_t,
	uuids *C.uuid_t,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	req := &control.SystemCheckStopReq{}

	// Convert pool UUIDs if provided
	if uuids != nil && poolNr > 0 {
		uuidSlice := unsafe.Slice(uuids, poolNr)
		for i := uint32(0); i < uint32(poolNr); i++ {
			u := uuidFromC(&uuidSlice[i])
			if u != uuid.Nil {
				req.CheckStopReq.Uuids = append(req.CheckStopReq.Uuids, u.String())
			}
		}
	}

	err := control.SystemCheckStop(ctx.ctx(), ctx.client, req)
	return C.int(errorToRC(err))
}

//export daos_control_check_query
func daos_control_check_query(
	handle C.uintptr_t,
	poolNr C.uint32_t,
	uuids *C.uuid_t,
	dci *C.struct_daos_check_info,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	req := &control.SystemCheckQueryReq{}

	// Build list of UUIDs to query
	if uuids != nil && poolNr > 0 {
		uuidSlice := unsafe.Slice(uuids, poolNr)
		for i := uint32(0); i < uint32(poolNr); i++ {
			u := uuidFromC(&uuidSlice[i])
			if u != uuid.Nil {
				req.CheckQueryReq.Uuids = append(req.CheckQueryReq.Uuids, u.String())
			}
		}
	}

	resp, err := control.SystemCheckQuery(ctx.ctx(), ctx.client, req)
	if err != nil {
		return C.int(errorToRC(err))
	}

	if dci == nil {
		return 0
	}

	// Zero-initialize the output struct to avoid uninitialized memory reads
	// (e.g. when pool/report conditions below are not met).
	C.memset(unsafe.Pointer(dci), 0, C.size_t(unsafe.Sizeof(*dci)))

	// Populate the daos_check_info structure
	dci.dci_status = C.CString(resp.Status.String())
	dci.dci_phase = C.CString(resp.ScanPhase.String())

	// Populate pools if requested
	queryUUIDs := req.CheckQueryReq.Uuids
	if len(queryUUIDs) > 0 && len(resp.Pools) > 0 {
		// Count matching pools
		matchingPools := 0
		for _, qUUID := range queryUUIDs {
			if _, ok := resp.Pools[qUUID]; ok {
				matchingPools++
			}
		}

		if matchingPools > 0 {
			dci.dci_pools = (*C.struct_daos_check_pool_info)(C.calloc(C.size_t(matchingPools),
				C.size_t(unsafe.Sizeof(C.struct_daos_check_pool_info{}))))
			poolSlice := unsafe.Slice(dci.dci_pools, matchingPools)

			idx := 0
			for _, qUUID := range queryUUIDs {
				pool, ok := resp.Pools[qUUID]
				if !ok {
					continue
				}

				// Parse and copy UUID
				if parsedUUID, err := uuid.Parse(pool.UUID); err == nil {
					copyUUIDToC(parsedUUID, &poolSlice[idx].dcpi_uuid)
				}
				poolSlice[idx].dcpi_status = C.CString(pool.Status)
				poolSlice[idx].dcpi_phase = C.CString(pool.Phase)
				idx++
			}
			dci.dci_pool_nr = C.int(idx)
		}
	}

	// Populate reports
	if len(resp.Reports) > 0 {
		dci.dci_reports = (*C.struct_daos_check_report_info)(C.calloc(C.size_t(len(resp.Reports)),
			C.size_t(unsafe.Sizeof(C.struct_daos_check_report_info{}))))
		reportSlice := unsafe.Slice(dci.dci_reports, len(resp.Reports))

		for i, rpt := range resp.Reports {
			// Parse and copy pool UUID
			if parsedUUID, err := uuid.Parse(rpt.PoolUuid); err == nil {
				copyUUIDToC(parsedUUID, &reportSlice[i].dcri_uuid)
			}
			reportSlice[i].dcri_seq = C.uint64_t(rpt.Seq)
			reportSlice[i].dcri_class = C.uint32_t(rpt.Class)
			reportSlice[i].dcri_act = C.uint32_t(rpt.Action)
			reportSlice[i].dcri_result = C.int(rpt.Result)

			// Copy action choices
			nChoices := len(rpt.ActChoices)
			if nChoices > 4 {
				nChoices = 4
			}
			reportSlice[i].dcri_option_nr = C.int(nChoices)
			for j := 0; j < nChoices; j++ {
				reportSlice[i].dcri_options[j] = C.int(rpt.ActChoices[j])
			}
		}
		dci.dci_report_nr = C.int(len(resp.Reports))
	}

	return 0
}

//export daos_control_check_info_free
func daos_control_check_info_free(dci *C.struct_daos_check_info) {
	if dci == nil {
		return
	}

	if dci.dci_status != nil {
		C.free(unsafe.Pointer(dci.dci_status))
		dci.dci_status = nil
	}
	if dci.dci_phase != nil {
		C.free(unsafe.Pointer(dci.dci_phase))
		dci.dci_phase = nil
	}

	if dci.dci_pools != nil {
		poolSlice := unsafe.Slice(dci.dci_pools, dci.dci_pool_nr)
		for i := 0; i < int(dci.dci_pool_nr); i++ {
			if poolSlice[i].dcpi_status != nil {
				C.free(unsafe.Pointer(poolSlice[i].dcpi_status))
			}
			if poolSlice[i].dcpi_phase != nil {
				C.free(unsafe.Pointer(poolSlice[i].dcpi_phase))
			}
		}
		C.free(unsafe.Pointer(dci.dci_pools))
		dci.dci_pools = nil
		dci.dci_pool_nr = 0
	}

	if dci.dci_reports != nil {
		C.free(unsafe.Pointer(dci.dci_reports))
		dci.dci_reports = nil
		dci.dci_report_nr = 0
	}
}

//export daos_control_check_repair
func daos_control_check_repair(
	handle C.uintptr_t,
	seq C.uint64_t,
	action C.uint32_t,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	req := &control.SystemCheckRepairReq{}
	req.CheckActReq.Seq = uint64(seq)
	if err := req.SetAction(int32(action)); err != nil {
		return C.int(daos.InvalidInput)
	}

	err := control.SystemCheckRepair(ctx.ctx(), ctx.client, req)
	return C.int(errorToRC(err))
}

//export daos_control_check_set_policy
func daos_control_check_set_policy(
	handle C.uintptr_t,
	flags C.uint32_t,
	policies *C.char,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	req := &control.SystemCheckSetPolicyReq{}

	// TCPF_RESET = (1 << 0), TCPF_INTERACT = (1 << 1)
	const (
		tcpfReset    = 1 << 0
		tcpfInteract = 1 << 1
	)

	if uint32(flags)&tcpfReset != 0 {
		req.ResetToDefaults = true
	}
	if uint32(flags)&tcpfInteract != 0 {
		req.AllInteractive = true
	}

	// Parse policies string if provided (format: "CLASS:ACTION,CLASS:ACTION,...")
	if policies != nil {
		policiesStr := goString(policies)
		if policiesStr != "" {
			parsed, err := parsePolicies(policiesStr)
			if err != nil {
				return C.int(errorToRC(err))
			}
			req.Policies = parsed
		}
	}

	err := control.SystemCheckSetPolicy(ctx.ctx(), ctx.client, req)
	return C.int(errorToRC(err))
}
