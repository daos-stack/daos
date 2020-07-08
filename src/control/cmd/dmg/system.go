//
// (C) Copyright 2019-2020 Intel Corporation.
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
	"bytes"
	"context"
	"fmt"
	"os"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

// SystemCmd is the struct representing the top-level system subcommand.
type SystemCmd struct {
	LeaderQuery leaderQueryCmd     `command:"leader-query" alias:"l" description:"Query for current Management Service leader"`
	Query       systemQueryCmd     `command:"query" alias:"q" description:"Query DAOS system status"`
	Stop        systemStopCmd      `command:"stop" alias:"s" description:"Perform controlled shutdown of DAOS system"`
	Start       systemStartCmd     `command:"start" alias:"r" description:"Perform start of stopped DAOS system"`
	ListPools   systemListPoolsCmd `command:"list-pools" alias:"p" description:"List all pools in the DAOS system"`
}

type leaderQueryCmd struct {
	logCmd
	cfgCmd
	ctlInvokerCmd
	jsonOutputCmd
}

func (cmd *leaderQueryCmd) Execute(_ []string) error {
	ctx := context.Background()
	resp, err := control.LeaderQuery(ctx, cmd.ctlInvoker, &control.LeaderQueryReq{
		System: cmd.config.SystemName,
	})
	if err != nil {
		return errors.Wrap(err, "leader query failed")
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(os.Stdout, resp)
	}

	cmd.log.Infof("Current Leader: %s\n   Replica Set: %s\n", resp.Leader,
		strings.Join(resp.Replicas, ", "))
	return nil
}

// rankStateGroups initialises groupings of ranks that are at a particular state.
func rankStateGroups(members system.Members) (system.RankGroups, error) {
	ranksInState := make(map[system.MemberState]*bytes.Buffer)
	ranksSeen := make(map[system.Rank]struct{})

	for _, m := range members {
		if _, exists := ranksSeen[m.Rank]; exists {
			return nil, system.FaultMemberExists(m.Rank)
		}
		ranksSeen[m.Rank] = struct{}{}

		if _, exists := ranksInState[m.State()]; !exists {
			ranksInState[m.State()] = new(bytes.Buffer)
		}
		fmt.Fprintf(ranksInState[m.State()], "%d,", m.Rank)
	}

	groups := make(system.RankGroups)
	for state, ranksStrBuf := range ranksInState {
		rankSet, err := system.NewRankSet(
			strings.TrimSuffix(ranksStrBuf.String(), ","))
		if err != nil {
			return nil, errors.WithMessage(err,
				"generating groups of ranks at state")
		}
		groups[state.String()] = rankSet
	}

	return groups, nil
}

func displaySystemQuery(log logging.Logger, members system.Members) error {
	groups, err := rankStateGroups(members)
	if err != nil {
		return err
	}
	out, err := tabulateRankGroups(groups, "Rank", "State")
	if err != nil {
		return err
	}

	log.Info(out)

	return nil
}

func displaySystemQueryVerbose(log logging.Logger, members system.Members) {
	rankTitle := "Rank"
	uuidTitle := "UUID"
	addrTitle := "Control Address"
	stateTitle := "State"
	reasonTitle := "Reason"

	formatter := txtfmt.NewTableFormatter(rankTitle, uuidTitle, addrTitle, stateTitle, reasonTitle)
	var table []txtfmt.TableRow

	for _, m := range members {
		row := txtfmt.TableRow{rankTitle: fmt.Sprintf("%d", m.Rank)}
		row[uuidTitle] = m.UUID.String()
		row[addrTitle] = m.Addr.String()
		row[stateTitle] = m.State().String()
		row[reasonTitle] = m.Info

		table = append(table, row)
	}

	log.Info(formatter.Format(table))
}

func displaySystemQuerySingle(log logging.Logger, members system.Members) error {
	if len(members) != 1 {
		return errors.Errorf("expected 1 member in result, got %d", len(members))
	}

	m := members[0]

	table := []txtfmt.TableRow{
		{"address": m.Addr.String()},
		{"uuid": m.UUID.String()},
		{"status": m.State().String()},
		{"reason": m.Info},
	}

	title := fmt.Sprintf("Rank %d", m.Rank)
	log.Info(txtfmt.FormatEntity(title, table))

	return nil
}

// systemQueryCmd is the struct representing the command to query system status.
type systemQueryCmd struct {
	logCmd
	ctlInvokerCmd
	jsonOutputCmd
	Verbose bool   `long:"verbose" short:"v" description:"Display more member details"`
	Ranks   string `long:"ranks" short:"r" description:"Comma separated ranges or individual system ranks to query"`
}

// Execute is run when systemQueryCmd activates
func (cmd *systemQueryCmd) Execute(_ []string) error {
	ranks, err := system.ParseRanks(cmd.Ranks)
	if err != nil {
		return errors.Wrap(err, "parsing rank list")
	}

	ctx := context.Background()
	// TODO DAOS-5079: group errors when ranks don't exist
	resp, err := control.SystemQuery(ctx, cmd.ctlInvoker,
		&control.SystemQueryReq{Ranks: ranks})
	if err != nil {
		return errors.Wrap(err, "System-Query command failed")
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(os.Stdout, resp)
	}

	cmd.log.Debug("System-Query command succeeded")
	if len(resp.Members) == 0 {
		cmd.log.Info("No members in system")
		return nil
	}

	if len(cmd.Ranks) == 1 {
		return displaySystemQuerySingle(cmd.log, resp.Members)
	}

	if cmd.Verbose {
		displaySystemQueryVerbose(cmd.log, resp.Members)
		return nil
	}

	return displaySystemQuery(cmd.log, resp.Members)
}

// rankActionGroups initialises groupings of ranks that return the same results.
func rankActionGroups(results system.MemberResults) (system.RankGroups, error) {
	ranksWithResult := make(map[string]*bytes.Buffer)
	ranksSeen := make(map[system.Rank]struct{})

	for _, r := range results {
		if _, exists := ranksSeen[r.Rank]; exists {
			return nil, system.FaultMemberExists(r.Rank)
		}
		ranksSeen[r.Rank] = struct{}{}

		msg := "OK"
		if r.Errored {
			msg = r.Msg
		}
		if r.Action == "" {
			return nil, errors.Errorf(
				"action field empty for rank %d result", r.Rank)
		}

		resStr := fmt.Sprintf("%s%s%s", r.Action, rowFieldSep, msg)
		if _, exists := ranksWithResult[resStr]; !exists {
			ranksWithResult[resStr] = new(bytes.Buffer)
		}
		fmt.Fprintf(ranksWithResult[resStr], "%d,", r.Rank)
	}

	groups := make(system.RankGroups)
	for strResult, ranksStrBuf := range ranksWithResult {
		rankSet, err := system.NewRankSet(
			strings.TrimSuffix(ranksStrBuf.String(), ","))
		if err != nil {
			return nil, errors.WithMessage(err,
				"generating groups of ranks with same result")
		}
		groups[strResult] = rankSet
	}

	return groups, nil
}

func displaySystemAction(log logging.Logger, results system.MemberResults) error {
	groups, err := rankActionGroups(results)
	if err != nil {
		return err
	}
	out, err := tabulateRankGroups(groups, "Rank", "Operation", "Result")
	if err != nil {
		return errors.Wrap(err, "printing result table")
	}

	log.Info(out)

	return nil
}

// systemStopCmd is the struct representing the command to shutdown DAOS system.
type systemStopCmd struct {
	logCmd
	ctlInvokerCmd
	jsonOutputCmd
	Force bool   `long:"force" description:"Force stop DAOS system members"`
	Ranks string `long:"ranks" short:"r" description:"Comma separated list of system ranks to query"`
}

// Execute is run when systemStopCmd activates
//
// Perform prep and kill stages with stop command.
func (cmd *systemStopCmd) Execute(_ []string) error {
	ranks, err := system.ParseRanks(cmd.Ranks)
	if err != nil {
		return errors.Wrap(err, "parsing rank list")
	}

	ctx := context.Background()
	// TODO DAOS-5079: group errors when ranks don't exist
	resp, err := control.SystemStop(ctx, cmd.ctlInvoker, &control.SystemStopReq{
		Prep:  true,
		Kill:  true,
		Force: cmd.Force,
		Ranks: ranks,
	})
	if err != nil {
		return errors.Wrap(err, "System-Stop command failed")
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(os.Stdout, resp)
	}

	if len(resp.Results) == 0 {
		cmd.log.Debug("System-Stop no results returned")
		return nil
	}
	cmd.log.Debug("System-Stop command succeeded")

	return displaySystemAction(cmd.log, resp.Results)
}

// systemStartCmd is the struct representing the command to start system.
type systemStartCmd struct {
	logCmd
	ctlInvokerCmd
	jsonOutputCmd
	Ranks string `long:"ranks" short:"r" description:"Comma separated list of system ranks to query"`
}

// Execute is run when systemStartCmd activates
func (cmd *systemStartCmd) Execute(_ []string) error {
	ranks, err := system.ParseRanks(cmd.Ranks)
	if err != nil {
		return errors.Wrap(err, "parsing rank list")
	}

	ctx := context.Background()
	// TODO DAOS-5079: group errors when ranks don't exist
	resp, err := control.SystemStart(ctx, cmd.ctlInvoker,
		&control.SystemStartReq{Ranks: ranks})
	if err != nil {
		return errors.Wrap(err, "System-Start command failed")
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(os.Stdout, resp)
	}

	if len(resp.Results) == 0 {
		cmd.log.Debug("System-Start no results returned")
		return nil
	}
	cmd.log.Debug("System-Start command succeeded")

	return displaySystemAction(cmd.log, resp.Results)
}

// systemListPoolsCmd represents the command to fetch a list of all DAOS pools in the system.
type systemListPoolsCmd struct {
	logCmd
	cfgCmd
	ctlInvokerCmd
	jsonOutputCmd
}

func formatPoolSvcReps(svcReps []uint32) string {
	var b strings.Builder
	for i, rep := range svcReps {
		if i != 0 {
			b.WriteString(",")
		}
		fmt.Fprintf(&b, "%d", rep)
	}

	return b.String()
}

// Execute is run when systemListPoolsCmd activates
func (cmd *systemListPoolsCmd) Execute(_ []string) error {
	if cmd.config == nil {
		return errors.New("No configuration loaded")
	}

	ctx := context.Background()
	resp, err := control.ListPools(ctx, cmd.ctlInvoker, &control.ListPoolsReq{
		System: cmd.config.SystemName,
	})
	if err != nil {
		return errors.Wrap(err, "List-Pools command failed")
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(os.Stdout, resp)
	}

	if len(resp.Pools) == 0 {
		cmd.log.Info("No pools in system\n")
		return nil
	}

	uuidTitle := "Pool UUID"
	svcRepTitle := "Svc Replicas"

	formatter := txtfmt.NewTableFormatter(uuidTitle, svcRepTitle)
	var table []txtfmt.TableRow

	for _, pool := range resp.Pools {
		row := txtfmt.TableRow{uuidTitle: pool.UUID}

		if len(pool.SvcReplicas) != 0 {
			row[svcRepTitle] = formatPoolSvcReps(pool.SvcReplicas)
		}

		table = append(table, row)
	}

	cmd.log.Info(formatter.Format(table))
	return nil
}
