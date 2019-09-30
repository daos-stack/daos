//
// (C) Copyright 2019 Intel Corporation.
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
	"fmt"

	"github.com/pkg/errors"
)

// systemCmd is the struct representing the top-level system subcommand.
type systemCmd struct {
	systemQuery systemQueryCmd `command:"query" alias:"q" description:"Retrieve DAOS system membership"`
	systemStop  systemStopCmd  `command:"stop" alias:"s" description:"Perform controlled shutdown of DAOS system"`
}

// systemStopCmd is the struct representing the command to shutdown system.
type systemStopCmd struct {
	logCmd
	connectedCmd
}

// Execute is run when systemStopCmd activates
func (cmd *systemStopCmd) Execute(args []string) error {
	msg := "SUCCEEDED: "

	resp, err := cmd.conns.SystemStop()
	if err != nil {
		msg = errors.WithMessagef(err, "FAILED").Error()
	}
	if resp.Members != nil && len(resp.Members) > 0 {
		msg += fmt.Sprintf(": %+v", resp)
	}

	log.Infof("System-stop command %s\n", msg)

	return nil
}

// systemQueryCmd is the struct representing the command to shutdown system.
type systemQueryCmd struct {
	logCmd
	connectedCmd
}

// Execute is run when systemQueryCmd activates
func (cmd *systemQueryCmd) Execute(args []string) error {
	msg := "SUCCEEDED: "

	resp, err := cmd.conns.SystemQuery()
	if err != nil {
		msg = errors.WithMessagef(err, "FAILED").Error()
	}
	if resp.Members != nil && len(resp.Members) > 0 {
		msg += fmt.Sprintf(": %+v", resp)
	}

	log.Infof("System-query command %s\n", msg)

	return nil
}
