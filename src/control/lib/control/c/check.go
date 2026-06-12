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
	"fmt"
	"sort"
	"strings"
	"unsafe"

	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
)

// parsePolicies parses a comma-separated "CLASS:ACTION" policy string.
// Errors include the offending element's index and wrap daos.InvalidInput
// so errorToRC reduces them to a single rc at the cgo boundary.
func parsePolicies(policiesStr string) ([]*control.SystemCheckPolicy, error) {
	var policies []*control.SystemCheckPolicy

	for i, policyStr := range strings.Split(policiesStr, ",") {
		policyStr = strings.TrimSpace(policyStr)
		if policyStr == "" {
			continue
		}
		parts := strings.SplitN(policyStr, ":", 2)
		if len(parts) != 2 {
			return nil, fmt.Errorf("policy[%d] %q: missing CLASS:ACTION separator: %w",
				i, policyStr, daos.InvalidInput)
		}
		policy, err := control.NewSystemCheckPolicy(parts[0], parts[1])
		if err != nil {
			return nil, fmt.Errorf("policy[%d] %q: %v: %w",
				i, policyStr, err, daos.InvalidInput)
		}
		policies = append(policies, policy)
	}

	return policies, nil
}

// uuidStringsFromC translates an array of C uuid_t values (length poolNr)
// into canonical Go UUID strings, skipping zero UUIDs.
func uuidStringsFromC(uuids *C.uuid_t, poolNr C.uint32_t) []string {
	if uuids == nil || poolNr == 0 {
		return nil
	}
	slice := unsafe.Slice(uuids, poolNr)
	out := make([]string, 0, poolNr)
	for i := range slice {
		if u := uuidFromC(&slice[i]); u != uuid.Nil {
			out = append(out, u.String())
		}
	}
	return out
}

//export daos_control_check_switch
func daos_control_check_switch(handle C.uintptr_t, enable C.int) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		if enable != 0 {
			return control.SystemCheckEnable(ctx.ctx(), ctx.client, &control.SystemCheckEnableReq{})
		}
		return control.SystemCheckDisable(ctx.ctx(), ctx.client, &control.SystemCheckDisableReq{})
	})
}

//export daos_control_check_start
func daos_control_check_start(handle C.uintptr_t, flags, poolNr C.uint32_t, uuids *C.uuid_t, policies *C.char) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		req := &control.SystemCheckStartReq{}
		req.CheckStartReq.Flags = uint32(flags)
		req.CheckStartReq.Uuids = uuidStringsFromC(uuids, poolNr)

		if policiesStr := goString(policies); policiesStr != "" {
			parsed, err := parsePolicies(policiesStr)
			if err != nil {
				ctx.log.Errorf("parsePolicies: %v", err)
				return err
			}
			req.Policies = parsed
		}
		return control.SystemCheckStart(ctx.ctx(), ctx.client, req)
	})
}

//export daos_control_check_stop
func daos_control_check_stop(handle C.uintptr_t, poolNr C.uint32_t, uuids *C.uuid_t) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		req := &control.SystemCheckStopReq{}
		req.CheckStopReq.Uuids = uuidStringsFromC(uuids, poolNr)
		return control.SystemCheckStop(ctx.ctx(), ctx.client, req)
	})
}

//export daos_control_check_query
func daos_control_check_query(handle C.uintptr_t, poolNr C.uint32_t, uuids *C.uuid_t, dci *C.struct_daos_check_info) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		req := &control.SystemCheckQueryReq{}
		req.CheckQueryReq.Uuids = uuidStringsFromC(uuids, poolNr)

		resp, err := control.SystemCheckQuery(ctx.ctx(), ctx.client, req)
		if err != nil {
			return err
		}

		if dci == nil {
			return nil
		}

		// Zero-init so conditionally-set pointer fields stay NULL.
		C.memset(unsafe.Pointer(dci), 0, C.size_t(unsafe.Sizeof(*dci)))

		dci.dci_status = C.CString(resp.Status.String())
		dci.dci_phase = C.CString(resp.ScanPhase.String())
		dci.dci_leader = C.int(resp.Leader)

		// Empty queryUUIDs means "report on every pool the server returned";
		// a non-empty list filters to those UUIDs. Sort the no-filter case
		// by UUID so output order is deterministic across map iterations.
		var includeUUIDs []string
		if len(req.CheckQueryReq.Uuids) > 0 {
			for _, qUUID := range req.CheckQueryReq.Uuids {
				if _, ok := resp.Pools[qUUID]; ok {
					includeUUIDs = append(includeUUIDs, qUUID)
				}
			}
		} else if len(resp.Pools) > 0 {
			includeUUIDs = make([]string, 0, len(resp.Pools))
			for u := range resp.Pools {
				includeUUIDs = append(includeUUIDs, u)
			}
			sort.Strings(includeUUIDs)
		}

		if len(includeUUIDs) > 0 {
			dci.dci_pools = (*C.struct_daos_check_pool_info)(C.calloc(C.size_t(len(includeUUIDs)),
				C.size_t(unsafe.Sizeof(C.struct_daos_check_pool_info{}))))
			if dci.dci_pools == nil {
				daos_control_check_info_free(dci)
				return daos.NoMemory
			}
			poolSlice := unsafe.Slice(dci.dci_pools, len(includeUUIDs))

			for i, u := range includeUUIDs {
				pool := resp.Pools[u]
				if parsedUUID, err := uuid.Parse(pool.UUID); err == nil {
					copyUUIDToC(parsedUUID, &poolSlice[i].dcpi_uuid)
				}
				poolSlice[i].dcpi_status = C.CString(pool.Status)
				poolSlice[i].dcpi_phase = C.CString(pool.Phase)
			}
			dci.dci_pool_nr = C.int(len(includeUUIDs))
		}

		if len(resp.Reports) > 0 {
			dci.dci_reports = (*C.struct_daos_check_report_info)(C.calloc(C.size_t(len(resp.Reports)),
				C.size_t(unsafe.Sizeof(C.struct_daos_check_report_info{}))))
			if dci.dci_reports == nil {
				daos_control_check_info_free(dci)
				return daos.NoMemory
			}
			reportSlice := unsafe.Slice(dci.dci_reports, len(resp.Reports))

			for i, rpt := range resp.Reports {
				if parsedUUID, err := uuid.Parse(rpt.PoolUuid); err == nil {
					copyUUIDToC(parsedUUID, &reportSlice[i].dcri_uuid)
				}
				reportSlice[i].dcri_seq = C.uint64_t(rpt.Seq)
				reportSlice[i].dcri_class = C.uint32_t(rpt.Class)
				reportSlice[i].dcri_act = C.uint32_t(rpt.Action)
				reportSlice[i].dcri_result = C.int(rpt.Result)
				reportSlice[i].dcri_rank = C.int(rpt.Rank)

				nChoices := len(rpt.ActChoices)
				if nChoices > C.DAOS_CHECK_MAX_ACT_OPTIONS {
					nChoices = C.DAOS_CHECK_MAX_ACT_OPTIONS
				}
				reportSlice[i].dcri_option_nr = C.int(nChoices)
				for j := 0; j < nChoices; j++ {
					reportSlice[i].dcri_options[j] = C.int(rpt.ActChoices[j])
				}
			}
			dci.dci_report_nr = C.int(len(resp.Reports))
		}

		return nil
	})
}

//export daos_control_check_info_free
func daos_control_check_info_free(dci *C.struct_daos_check_info) {
	defer recoverExportVoid()

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
func daos_control_check_repair(handle C.uintptr_t, seq C.uint64_t, action C.uint32_t) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		req := &control.SystemCheckRepairReq{}
		req.CheckActReq.Seq = uint64(seq)
		if err := req.SetAction(int32(action)); err != nil {
			return daos.InvalidInput
		}
		return control.SystemCheckRepair(ctx.ctx(), ctx.client, req)
	})
}

//export daos_control_check_set_policy
func daos_control_check_set_policy(handle C.uintptr_t, flags C.uint32_t, policies *C.char) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		req := &control.SystemCheckSetPolicyReq{}

		// TCPF_* flag values from chk.proto (SetSystemCheckPolicyFlags).
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

		if policiesStr := goString(policies); policiesStr != "" {
			parsed, err := parsePolicies(policiesStr)
			if err != nil {
				ctx.log.Errorf("parsePolicies: %v", err)
				return err
			}
			req.Policies = parsed
		}
		return control.SystemCheckSetPolicy(ctx.ctx(), ctx.client, req)
	})
}
