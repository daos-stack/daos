//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"strings"
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/control"
)

func TestDmg_ConfigCommands(t *testing.T) {
	runCmdTests(t, []cmdTest{
		{
			"Generate with defaults",
			"config generate",
			strings.Join([]string{
				printRequest(t, &control.NetworkScanReq{}),
			}, " "),
			errors.New("no host responses"),
		},
		{
			"Generate with no nvme",
			"config generate --min-ssds 0",
			strings.Join([]string{
				printRequest(t, &control.NetworkScanReq{}),
			}, " "),
			errors.New("no host responses"),
		},
		{
			"Generate with storage parameters",
			"config generate --num-engines 2 --min-ssds 4",
			strings.Join([]string{
				printRequest(t, &control.NetworkScanReq{}),
			}, " "),
			errors.New("no host responses"),
		},
		{
			"Generate with short option storage parameters",
			"config generate -e 2 -s 4",
			strings.Join([]string{
				printRequest(t, &control.NetworkScanReq{}),
			}, " "),
			errors.New("no host responses"),
		},
		{
			"Generate with ethernet network device class",
			"config generate --net-class ethernet",
			strings.Join([]string{
				printRequest(t, &control.NetworkScanReq{}),
			}, " "),
			errors.New("no host responses"),
		},
		{
			"Generate with infiniband network device class",
			"config generate --net-class infiniband",
			strings.Join([]string{
				printRequest(t, &control.NetworkScanReq{}),
			}, " "),
			errors.New("no host responses"),
		},
		{
			"Generate with best-available network device class",
			"config generate --net-class best-available",
			strings.Join([]string{
				printRequest(t, &control.NetworkScanReq{}),
			}, " "),
			errors.New("no host responses"),
		},
		{
			"Generate with unsupported network device class",
			"config generate --net-class loopback",
			strings.Join([]string{
				printRequest(t, &control.NetworkScanReq{}),
			}, " "),
			errors.New("Invalid value"),
		},
		{
			"Nonexistent subcommand",
			"network quack",
			"",
			errors.New("Unknown command"),
		},
	})
}
