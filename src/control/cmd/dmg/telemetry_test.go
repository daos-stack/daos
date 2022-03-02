//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/pkg/errors"
)

func TestTelemetryCommands(t *testing.T) {
	runCmdTests(t, []cmdTest{
		{
			"list with too many hosts",
			"telemetry metrics list -l host1,host2",
			"",
			errors.New("exactly 1 host"),
		},
		{
			"query with too many hosts",
			"telemetry metrics query -l host1,host2",
			"",
			errors.New("exactly 1 host"),
		},
	})
}

func TestTelemetry_getMetricsHost(t *testing.T) {
	for name, tc := range map[string]struct {
		list      []string
		expResult string
		expErr    error
	}{
		"one host": {
			list:      []string{"one"},
			expResult: "one",
		},
		"host with port": {
			list:      []string{"one:1234"},
			expResult: "one",
		},
		"too many hosts": {
			list:   []string{"one", "two"},
			expErr: errors.New("exactly 1 host"),
		},
		"no hosts": {
			expResult: "localhost",
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := getMetricsHost(tc.list)

			test.CmpErr(t, tc.expErr, err)
			test.AssertEqual(t, tc.expResult, result, "")
		})
	}
}
