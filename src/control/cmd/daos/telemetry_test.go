//
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos/api"
	"github.com/daos-stack/daos/src/control/lib/ui"
)

func TestDaos_containerEnableTelemetryCmd(t *testing.T) {
	baseArgs := test.JoinArgs(nil, "container", "telemetry", "enable")
	fullArgs := test.JoinArgs(baseArgs, defaultPoolInfo.Label, defaultContInfo.ContainerLabel)

	for name, tc := range map[string]struct {
		args    []string
		expErr  error
		expArgs containerEnableTelemetryCmd
		setup   func(t *testing.T)
	}{
		"invalid flag": {
			args:   test.JoinArgs(fullArgs, "--bad"),
			expErr: errors.New("unknown flag"),
		},
		"open fails": {
			args:   test.JoinArgs(fullArgs, "-C", "dump"),
			expErr: errors.New("whoops"),
			setup: func(t *testing.T) {
				prevErr := contOpenErr
				t.Cleanup(func() {
					contOpenErr = prevErr
				})
				contOpenErr = errors.New("whoops")
			},
		},
		"missing source container": {
			args:   test.JoinArgs(baseArgs, defaultPoolInfo.Label, "-C", "dump"),
			expErr: errors.New("no container"),
		},
		"missing required arguments": {
			args:   fullArgs,
			expErr: errors.New("DumpContainerID must be set if DumpPoolID is set"),
		},
		"success": {
			args: test.JoinArgs(fullArgs, "-C", "dump"),
			expArgs: containerEnableTelemetryCmd{
				DumpCont: ContainerID{argOrID: argOrID{LabelOrUUIDFlag: ui.LabelOrUUIDFlag{Label: "dump"}}},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(api.ResetTestStubs)
			if tc.setup != nil {
				tc.setup(t)
			}

			runCmdTest(t, tc.args, tc.expArgs, tc.expErr, "Container.Telemetry.Enable")
		})
	}
}

func TestDaos_containerDisableTelemetryCmd(t *testing.T) {
	baseArgs := test.JoinArgs(nil, "container", "telemetry", "disable")
	fullArgs := test.JoinArgs(baseArgs, defaultPoolInfo.Label, defaultContInfo.ContainerLabel)

	for name, tc := range map[string]struct {
		args    []string
		expErr  error
		expArgs containerDisableTelemetryCmd
		setup   func(t *testing.T)
	}{
		"invalid flag": {
			args:   test.JoinArgs(fullArgs, "--bad"),
			expErr: errors.New("unknown flag"),
		},
		"open fails": {
			args:   fullArgs,
			expErr: errors.New("whoops"),
			setup: func(t *testing.T) {
				prevErr := contOpenErr
				t.Cleanup(func() {
					contOpenErr = prevErr
				})
				contOpenErr = errors.New("whoops")
			},
		},
		"missing source container": {
			args:   test.JoinArgs(baseArgs, defaultPoolInfo.Label),
			expErr: errors.New("no container"),
		},
		"success": {
			args:    fullArgs,
			expArgs: containerDisableTelemetryCmd{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(api.ResetTestStubs)
			if tc.setup != nil {
				tc.setup(t)
			}

			runCmdTest(t, tc.args, tc.expArgs, tc.expErr, "Container.Telemetry.Disable")
		})
	}
}
