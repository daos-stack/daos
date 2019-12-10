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
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/client"
)

// SystemCmd is the struct representing the top-level system subcommand.
type SystemCmd struct {
	LeaderQuery leaderQueryCmd       `command:"leader-query" alias:"l" description:"Query for current Management Service leader"`
	MemberQuery systemMemberQueryCmd `command:"member-query" alias:"q" description:"Retrieve DAOS system membership"`
	Stop        systemStopCmd        `command:"stop" alias:"s" description:"Perform controlled shutdown of DAOS system"`
	ListPools   systemListPoolsCmd   `command:"list-pools" alias:"p" description:"List all pools in the DAOS system"`
}

type leaderQueryCmd struct {
	logCmd
	cfgCmd
	connectedCmd
}

func (cmd *leaderQueryCmd) Execute(_ []string) error {
	leader, replicas, err := cmd.conns.LeaderQuery(cmd.config.SystemName)
	if err != nil {
		return errors.Wrap(err, "leader query failed")
	}

	cmd.log.Infof("Current Leader: %s\n   Replica Set: %s\n", leader, strings.Join(replicas, ", "))
	return nil
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

// systemListPoolsCmd represents the command to fetch a list of all DAOS pools in the system.
type systemListPoolsCmd struct {
	logCmd
	connectedCmd
	cfgCmd
}

// Execute is run when systemListPoolsCmd activates
func (cmd *systemListPoolsCmd) Execute(args []string) error {
	if cmd.config == nil {
		return errors.New("No configuration loaded")
	}
	req := client.ListPoolsReq{SysName: cmd.config.SystemName}
	resp, err := cmd.conns.ListPools(req)
	if err != nil {
		return errors.Wrap(err, "List-Pools command failed")
	}

	cmd.log.Debug("List-Pools command succeeded\n")
	if len(resp.Pools) == 0 {
		cmd.log.Info("No pools in system\n")
		return nil
	}

	uuidTitle := "Pool UUID"
	svcRepTitle := "Svc Replicas"

	formatter := NewTableFormatter([]string{uuidTitle, svcRepTitle})
	var table []TableRow

	var b strings.Builder
	for _, pool := range resp.Pools {
		row := TableRow{uuidTitle: pool.UUID}

		for i, rep := range pool.SvcReplicas {
			if i != 0 {
				b.WriteString(",")
			}
			fmt.Fprintf(&b, "%d", rep)
		}

		if len(pool.SvcReplicas) != 0 {
			row[svcRepTitle] = b.String()
		}

		table = append(table, row)

		b.Reset()
	}

	cmd.log.Info(formatter.Format(table))
	return nil
}
