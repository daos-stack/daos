//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/control"
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
		cfg       *control.Config
		custom    []string
		expResult string
		expErr    error
	}{
		"custom list with one host": {
			custom:    []string{"one"},
			expResult: "one",
		},
		"custom list with too many hosts": {
			custom: []string{"one", "two"},
			expErr: errors.New("exactly 1 host"),
		},
		"config with one host": {
			cfg: &control.Config{
				HostList: []string{"host1"},
			},
			expResult: "host1",
		},
		"config with too many hosts": {
			cfg: &control.Config{
				HostList: []string{"host1", "host2"},
			},
			expErr: errors.New("exactly 1 host"),
		},
		"config with host and port": {
			cfg: &control.Config{
				HostList: []string{"host1:10001"},
			},
			expResult: "host1",
		},
		"no hosts": {
			cfg:    &control.Config{},
			expErr: errors.New("exactly 1 host"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := getMetricsHost(tc.cfg, tc.custom)

			common.CmpErr(t, tc.expErr, err)
			common.AssertEqual(t, tc.expResult, result, "")
		})
	}
}
