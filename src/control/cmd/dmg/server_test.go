//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/control"
)

func TestServerCommands(t *testing.T) {
	runCmdTests(t, []cmdTest{
		{
			"Set log masks with reset", // empty "Masks" string indicates reset
			"server set-logmasks",
			printRequest(t, &control.SetEngineLogMasksReq{Masks: ""}),
			nil,
		},
		{
			"Set log masks", // expects positional argument
			"server set-logmasks ERR,mgmt=DEBUG",
			printRequest(t, &control.SetEngineLogMasksReq{Masks: "ERR,mgmt=DEBUG"}),
			nil,
		},
		{
			"Set log masks with invalid flag",
			"server set-logmasks --masks ERR,mgmt=DEBUG",
			"",
			errors.New("unknown flag"),
		},
		{
			"Set log masks with too many args",
			"server set-logmasks masks ERR,mgmt=DEBUG DEBUG",
			"",
			errors.New("expected 0-1 positional args but got 3"),
		},
	})
}
