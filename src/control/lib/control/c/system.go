//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

/*
#include <stdlib.h>
#include <stdint.h>
#include <daos_types.h>
*/
import "C"
import (
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

//export daos_control_system_stop_rank
func daos_control_system_stop_rank(handle C.uintptr_t, rank C.d_rank_t, force C.int) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		req := &control.SystemStopReq{Force: force != 0}
		req.Ranks.Add(ranklist.Rank(rank))
		resp, err := control.SystemStop(ctx.ctx(), ctx.client, req)
		if err != nil {
			return err
		}
		return firstMemberStatus(resp.Results)
	})
}

//export daos_control_system_start_rank
func daos_control_system_start_rank(handle C.uintptr_t, rank C.d_rank_t) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		req := &control.SystemStartReq{}
		req.Ranks.Add(ranklist.Rank(rank))
		resp, err := control.SystemStart(ctx.ctx(), ctx.client, req)
		if err != nil {
			return err
		}
		return firstMemberStatus(resp.Results)
	})
}

//export daos_control_system_reint_rank
func daos_control_system_reint_rank(handle C.uintptr_t, rank C.d_rank_t) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		// Clear=true reintegrates a previously excluded rank.
		req := &control.SystemExcludeReq{Clear: true}
		req.Ranks.Add(ranklist.Rank(rank))
		resp, err := control.SystemExclude(ctx.ctx(), ctx.client, req)
		if err != nil {
			return err
		}
		return firstMemberStatus(resp.Results)
	})
}

//export daos_control_system_exclude_rank
func daos_control_system_exclude_rank(handle C.uintptr_t, rank C.d_rank_t) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		req := &control.SystemExcludeReq{Clear: false}
		req.Ranks.Add(ranklist.Rank(rank))
		resp, err := control.SystemExclude(ctx.ctx(), ctx.client, req)
		if err != nil {
			return err
		}
		return firstMemberStatus(resp.Results)
	})
}
