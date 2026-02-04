//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

/*
#include <stdlib.h>
#include <stdint.h>
*/
import "C"
import (
	"github.com/daos-stack/daos/src/control/lib/control"
)

//export daos_control_server_set_logmasks
func daos_control_server_set_logmasks(
	handle C.uintptr_t,
	masks *C.char,
	streams *C.char,
	subsystems *C.char,
) C.int {
	ctx, rc := getContext(handle)
	if rc != 0 {
		return rc
	}

	req := &control.SetEngineLogMasksReq{}

	if masks != nil {
		m := goString(masks)
		req.Masks = &m
	}
	if streams != nil {
		s := goString(streams)
		req.Streams = &s
	}
	if subsystems != nil {
		ss := goString(subsystems)
		req.Subsystems = &ss
	}

	resp, err := control.SetEngineLogMasks(ctx.ctx(), ctx.client, req)
	if err != nil {
		return C.int(errorToRC(err))
	}

	// Check for host errors in the response
	if resp.HostErrorsResp.GetHostErrors().ErrorCount() > 0 {
		// Return the first error
		for _, hes := range resp.HostErrorsResp.GetHostErrors() {
			return C.int(errorToRC(hes.HostError))
		}
	}

	return 0
}
