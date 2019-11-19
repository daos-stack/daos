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
type SystemCmd struct {
	MemberQuery systemMemberQueryCmd `command:"member-query" alias:"q" description:"Retrieve DAOS system membership"`
	Stop        systemStopCmd        `command:"stop" alias:"s" description:"Perform controlled shutdown of DAOS system"`
}

// systemStopCmd is the struct representing the command to shutdown system.
type systemStopCmd struct {
	logCmd
	connectedCmd
}

// Execute is run when systemStopCmd activates
func (cmd *systemStopCmd) Execute(args []string) error {
	msg := "SUCCEEDED: "

	members, err := cmd.conns.SystemStop()
	if err != nil {
		msg = errors.WithMessagef(err, "FAILED").Error()
	}
	if len(members) > 0 {
		msg += fmt.Sprintf(": still %d active members", len(members))
	}

	cmd.log.Infof("System-stop command %s\n", msg)

	return nil
}

// systemMemberQueryCmd is the struct representing the command to shutdown system.
type systemMemberQueryCmd struct {
	logCmd
	connectedCmd
}

// Execute is run when systemMemberQueryCmd activates
func (cmd *systemMemberQueryCmd) Execute(args []string) error {
	msg := "SUCCEEDED: "

	members, err := cmd.conns.SystemMemberQuery()
	switch {
	case err != nil:
		msg = errors.WithMessagef(err, "FAILED").Error()
	case len(members) > 0:
		msg += members.String()
	default:
		msg += "no joined members"
	}

	cmd.log.Infof("System-member-query command %s\n", msg)

	return nil
}
