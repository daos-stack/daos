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
//	"context"
//	"strings"

//	"github.com/pkg/errors"

//	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
//	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/telem"
)

type telemetryCmd struct {
	Tree telemetryScanCmd `command:"tree" description:"Show the telemetry in tree format"`
	List telemetryListCmd `command:"list" description:"Read/list the telemetry "`
}

type telemetryScanCmd struct {
	cfgCmd
	logCmd
	Rank int `short:"r" long:"rank" description:"Use this rank for telemetry data"`
	Path string `short:"p" long:"path" description:"Scan telemetry from this path"`
}

type telemetryListCmd struct {
	cfgCmd
	logCmd
	Rank int `short:"r" long:"rank" description:"Use this rank for telemetry data"`
	Path string `short:"p" long:"path" description:"List telemetry from this path"`
	Iterations int `short:"i" long:"iter" description:"Number of iterations to print before exiting"`
}

func (cmd *telemetryScanCmd) Execute(args []string) error {
	telemetry.ShowDirectoryTree(cmd.Rank, cmd.Path)
	return nil
}

func (cmd *telemetryListCmd) Execute(args []string) error {
	telemetry.ListMetrics(cmd.Rank, cmd.Path, cmd.Iterations)
	return nil
}
