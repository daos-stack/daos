//
// (C) Copyright 2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package main

import (
	"github.com/daos-stack/daos/src/control/lib/telem"
)

type telemCmd struct {
	Tree telemScanCmd `command:"tree" description:"Show the telemetry in tree format"`
	List telemListCmd `command:"list" description:"Read/list the telemetry "`
}

type telemScanCmd struct {
	cfgCmd
	logCmd
	Rank int       `short:"r" long:"rank" description:"Use this rank for telemetry data"`
	Path string    `short:"p" long:"path" description:"Scan telemetry from this path"`
	Iterations int `short:"i" long:"iter" description:"Number of iterations to print before exiting"`
}

type telemListCmd struct {
	cfgCmd
	logCmd
	Rank       int    `short:"r" long:"rank" description:"Use this rank for telemetry data"`
	Path       string `short:"p" long:"path" description:"List telemetry from this path"`
	Iterations int    `short:"i" long:"iter" description:"Number of iterations to print before exiting"`
	Counter    bool   `short:"c" long:"counter" description:"Include counters in the listing"`
	Duration   bool   `short:"d" long:"duration" description:"Include durations in the listing"`
	Gauge      bool   `short:"g" long:"gauge" description:"Include gauges in the listing"`
	Snapshot   bool   `short:"s" long:"snapshot" description:"Include timer snapshots in the listing"`
	Timestamp  bool   `short:"t" long:"timestamp" description:"Include timestamps in the listing"`
}

func (cmd *telemScanCmd) Execute(args []string) error {
	telemetry.ShowDirectoryTree(cmd.Rank, cmd.Path, cmd.Iterations)
	return nil
}

func (cmd *telemListCmd) Execute(args []string) error {
	var filter string

	// Didn't want to use CGO to include the header for these constants
	// Making a string representation instead which is parsed later
	if cmd.Counter {
		filter += "c"
	}

	if cmd.Duration {
		filter += "d"
	}

	if cmd.Gauge {
		filter += "g"
	}

	if cmd.Snapshot {
		filter += "s"
	}

	if cmd.Timestamp {
		filter += "t"
	}

	// If nothing added to the filter, allow everything
	if len(filter) == 0 {
		filter = "cdgst"
	}

	telemetry.ListMetrics(cmd.Rank, cmd.Path, cmd.Iterations, filter)
	return nil
}
