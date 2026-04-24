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
func daos_control_server_set_logmasks(handle C.uintptr_t, masks, streams, subsystems *C.char) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
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
			return err
		}

		// The rc surface has room for one status; log the full host-error set
		// before collapsing to the first errored host's code.
		hem := resp.HostErrorsResp.GetHostErrors()
		if hem.ErrorCount() > 0 {
			logAllHostErrors(ctx, hem)
			return firstHostStatus(hem)
		}
		return nil
	})
}
