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
func daos_control_system_stop_rank(
	handle C.uintptr_t,
	rank C.d_rank_t,
	force C.int,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	req := &control.SystemStopReq{
		Force: force != 0,
	}
	req.Ranks.Add(ranklist.Rank(rank))

	resp, err := control.SystemStop(ctx.ctx(), ctx.client, req)
	if err != nil {
		return C.int(errorToRC(err))
	}

	return C.int(errorToRC(resp.Errors()))
}

//export daos_control_system_start_rank
func daos_control_system_start_rank(
	handle C.uintptr_t,
	rank C.d_rank_t,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	req := &control.SystemStartReq{}
	req.Ranks.Add(ranklist.Rank(rank))

	resp, err := control.SystemStart(ctx.ctx(), ctx.client, req)
	if err != nil {
		return C.int(errorToRC(err))
	}

	return C.int(errorToRC(resp.Errors()))
}

//export daos_control_system_reint_rank
func daos_control_system_reint_rank(
	handle C.uintptr_t,
	rank C.d_rank_t,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	// SystemExclude with Clear=true clears the exclude state (reintegrates)
	req := &control.SystemExcludeReq{
		Clear: true,
	}
	req.Ranks.Add(ranklist.Rank(rank))

	resp, err := control.SystemExclude(ctx.ctx(), ctx.client, req)
	if err != nil {
		return C.int(errorToRC(err))
	}

	return C.int(errorToRC(resp.Errors()))
}

//export daos_control_system_exclude_rank
func daos_control_system_exclude_rank(
	handle C.uintptr_t,
	rank C.d_rank_t,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	req := &control.SystemExcludeReq{
		Clear: false,
	}
	req.Ranks.Add(ranklist.Rank(rank))

	resp, err := control.SystemExclude(ctx.ctx(), ctx.client, req)
	if err != nil {
		return C.int(errorToRC(err))
	}

	return C.int(errorToRC(resp.Errors()))
}
