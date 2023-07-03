//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/control"
)

func TestDmg_ServerCommands(t *testing.T) {
	masks := "ERR,mgmt=DEBUG"
	streams := "MGMT,IO"
	subsystems := "mISC"
	runCmdTests(t, []cmdTest{
		{
			"Reset log masks streams and subsystems",
			"server set-logmasks",
			printRequest(t, &control.SetEngineLogMasksReq{}),
			nil,
		},
		{
			"Set log masks",
			"server set-logmasks -m ERR,mgmt=DEBUG",
			printRequest(t, &control.SetEngineLogMasksReq{Masks: &masks}),
			nil,
		},
		{
			"Set log masks with invalid flag",
			"server set-logmasks --mask ERR,mgmt=DEBUG",
			"",
			errors.New("unknown flag"),
		},
		{
			"Set log masks with debug streams (DD_MASK)",
			"server set-logmasks -m ERR,mgmt=DEBUG -d MGMT,IO",
			printRequest(t, &control.SetEngineLogMasksReq{
				Masks:   &masks,
				Streams: &streams,
			}),
			nil,
		},
		{
			"Set log masks with debug streams and subsystems (DD_MASK,DD_SUBSYS)",
			"server set-logmasks -m ERR,mgmt=DEBUG -d MGMT,IO -s mISC",
			printRequest(t, &control.SetEngineLogMasksReq{
				Masks:      &masks,
				Streams:    &streams,
				Subsystems: &subsystems,
			}),
			nil,
		},
	})
}
