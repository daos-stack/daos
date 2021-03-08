//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"bytes"
	"context"
	"fmt"
	"strings"

	"github.com/dustin/go-humanize/english"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
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

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, err)
	}

	if err != nil {
		return errors.Wrap(err, "leader query failed")
	}

	cmd.log.Infof("Current Leader: %s\n   Replica Set: %s\n", resp.Leader,
		strings.Join(resp.Replicas, ", "))
	return nil
}

// rankStateGroups initializes groupings of ranks that are at a particular state.
func rankStateGroups(members system.Members) (system.RankGroups, error) {
	ranksInState := make(map[system.MemberState]*bytes.Buffer)
	ranksSeen := make(map[system.Rank]struct{})

	for _, m := range members {
		if _, exists := ranksSeen[m.Rank]; exists {
			return nil, &system.ErrMemberExists{Rank: m.Rank}
		}
		ranksSeen[m.Rank] = struct{}{}

		if _, exists := ranksInState[m.State()]; !exists {
			ranksInState[m.State()] = new(bytes.Buffer)
		}
		fmt.Fprintf(ranksInState[m.State()], "%d,", m.Rank)
	}

	groups := make(system.RankGroups)
	for state, ranksStrBuf := range ranksInState {
		rankSet, err := system.CreateRankSet(
			strings.TrimSuffix(ranksStrBuf.String(), ","))
		if err != nil {
			return nil, errors.WithMessage(err,
				"generating groups of ranks at state")
		}
		groups[state.String()] = rankSet
	}

	return groups, nil
}

func displaySystemQuery(log logging.Logger, members system.Members, absentRanks *system.RankSet) error {
	groups, err := rankStateGroups(members)
	if err != nil {
		return err
	}

	if absentRanks.Count() != 0 {
		groups["Unknown Rank"] = absentRanks
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
	faultDomainTitle := "Fault Domain"
	stateTitle := "State"
	reasonTitle := "Reason"

	formatter := txtfmt.NewTableFormatter(rankTitle, uuidTitle, addrTitle, faultDomainTitle, stateTitle, reasonTitle)
	var table []txtfmt.TableRow

	for _, m := range members {
		row := txtfmt.TableRow{rankTitle: fmt.Sprintf("%d", m.Rank)}
		row[uuidTitle] = m.UUID.String()
		row[addrTitle] = m.Addr.String()
		row[faultDomainTitle] = m.FaultDomain.String()
		row[stateTitle] = m.State().String()
		row[reasonTitle] = m.Info

		table = append(table, row)
	}

	log.Info(formatter.Format(table))
}

func displaySystemQuerySingle(log logging.Logger, members system.Members) {
	m := members[0]

	table := []txtfmt.TableRow{
		{"address": m.Addr.String()},
		{"uuid": m.UUID.String()},
		{"fault domain": m.FaultDomain.String()},
		{"status": m.State().String()},
		{"reason": m.Info},
	}

	title := fmt.Sprintf("Rank %d", m.Rank)
	log.Info(txtfmt.FormatEntity(title, table))
}

// rankListCmd enables rank or host list to be supplied with command to filter
// which ranks are operated upon.
type rankListCmd struct {
	Ranks string `long:"ranks" short:"r" description:"Comma separated ranges or individual system ranks to operate on"`
	Hosts string `long:"rank-hosts" description:"Hostlist representing hosts whose managed ranks are to be operated on"`
}

// validateHostsRanks validates rank and host lists have correct format.
//
// Populate request with valid list strings.
func (cmd *rankListCmd) validateHostsRanks() (outHosts *hostlist.HostSet, outRanks *system.RankSet, err error) {
	outHosts, err = hostlist.CreateSet(cmd.Hosts)
	if err != nil {
		return
	}
	outRanks, err = system.CreateRankSet(cmd.Ranks)
	if err != nil {
		return
	}

	if outHosts.Count() > 0 && outRanks.Count() > 0 {
		err = errors.New("--ranks and --rank-hosts options cannot be set together")
	}

	return
}

// systemQueryCmd is the struct representing the command to query system status.
type systemQueryCmd struct {
	logCmd
	ctlInvokerCmd
	jsonOutputCmd
	rankListCmd
	Verbose bool `long:"verbose" short:"v" description:"Display more member details"`
}

// Execute is run when systemQueryCmd activates
func (cmd *systemQueryCmd) Execute(_ []string) error {
	hostSet, rankSet, err := cmd.validateHostsRanks()
	if err != nil {
		return err
	}
	req := new(control.SystemQueryReq)
	req.Hosts.ReplaceSet(hostSet)
	req.Ranks.ReplaceSet(rankSet)

	resp, err := control.SystemQuery(context.Background(), cmd.ctlInvoker, req)
	errCmd := errors.Wrap(err, "System-Query command")
	errResp := errors.Wrap(resp.Errors(), "System-Query command")

	if cmd.jsonOutputEnabled() {
		if errCmd != nil {
			return cmd.outputJSON(resp, errCmd)
		}

		return cmd.outputJSON(resp, errResp)
	}

	if errCmd != nil {
		return errCmd
	}

	cmd.log.Debugf("System-Query command succeeded, absent hosts: %s, absent ranks: %s",
		resp.AbsentHosts.String(), resp.AbsentRanks.String())

	var absentRanksPrinted bool
	switch {
	case len(resp.Members) == 0:
		cmd.log.Info("Query matches no members in system.")
	case len(resp.Members) == 1:
		displaySystemQuerySingle(cmd.log, resp.Members)
	case cmd.Verbose:
		displaySystemQueryVerbose(cmd.log, resp.Members)
	default:
		if errDisplay := displaySystemQuery(cmd.log, resp.Members,
			&resp.AbsentRanks); errDisplay != nil {
			return errDisplay
		}
		absentRanksPrinted = true
	}

	cmd.log.Info("\n")

	if resp.AbsentHosts.Count() > 0 {
		cmd.log.Infof("Unknown %s: %s",
			english.Plural(resp.AbsentHosts.Count(), "host", "hosts"),
			resp.AbsentHosts.String())
	}
	if !absentRanksPrinted && resp.AbsentRanks.Count() > 0 {
		cmd.log.Infof("Unknown %s: %s",
			english.Plural(resp.AbsentRanks.Count(), "rank", "ranks"),
			resp.AbsentRanks.String())
	}

	return errResp
}

// rankActionGroups initializes groupings of ranks that return the same results.
func rankActionGroups(results system.MemberResults) (system.RankGroups, error) {
	ranksWithResult := make(map[string]*bytes.Buffer)
	ranksSeen := make(map[system.Rank]struct{})

	for _, r := range results {
		if _, exists := ranksSeen[r.Rank]; exists {
			return nil, &system.ErrMemberExists{Rank: r.Rank}
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
		rankSet, err := system.CreateRankSet(
			strings.TrimSuffix(ranksStrBuf.String(), ","))
		if err != nil {
			return nil, errors.WithMessage(err,
				"generating groups of ranks with same result")
		}
		groups[strResult] = rankSet
	}

	return groups, nil
}

func displaySystemAction(log logging.Logger, results system.MemberResults,
	absentHosts *hostlist.HostSet, absentRanks *system.RankSet) error {

	groups, err := rankActionGroups(results)
	if err != nil {
		return err
	}

	if absentRanks.Count() > 0 {
		groups[fmt.Sprintf("----%sUnknown Rank", rowFieldSep)] = absentRanks
	}

	out, err := tabulateRankGroups(groups, "Rank", "Operation", "Result")
	if err != nil {
		return errors.Wrap(err, "printing result table")
	}

	if absentHosts.Count() > 0 {
		log.Infof("%s\nUnknown %s: %s", out,
			english.Plural(absentHosts.Count(), "host", "hosts"),
			absentHosts.String())
	} else {
		log.Infof("%s\n", out)
	}

	return nil
}

// systemStopCmd is the struct representing the command to shutdown DAOS system.
type systemStopCmd struct {
	logCmd
	ctlInvokerCmd
	jsonOutputCmd
	rankListCmd
	Force bool `long:"force" description:"Force stop DAOS system members"`
}

// Execute is run when systemStopCmd activates
//
// Perform prep and kill stages with stop command.
func (cmd *systemStopCmd) Execute(_ []string) error {
	hostSet, rankSet, err := cmd.validateHostsRanks()
	if err != nil {
		return err
	}
	req := &control.SystemStopReq{Prep: true, Kill: true, Force: cmd.Force}
	req.Hosts.ReplaceSet(hostSet)
	req.Ranks.ReplaceSet(rankSet)

	resp, err := control.SystemStop(context.Background(), cmd.ctlInvoker, req)
	errCmd := errors.Wrap(err, "System-Stop command")
	errResp := errors.Wrap(resp.Errors(), "System-Stop command")

	if cmd.jsonOutputEnabled() {
		if errCmd != nil {
			return cmd.outputJSON(resp, errCmd)
		}

		return cmd.outputJSON(resp, errResp)
	}

	if errCmd != nil {
		return errCmd
	}

	cmd.log.Debugf("System-Stop command: absent hosts: %s, absent ranks: %s",
		resp.AbsentHosts.String(), resp.AbsentRanks.String())

	if errDisplay := displaySystemAction(cmd.log, resp.Results,
		&resp.AbsentHosts, &resp.AbsentRanks); errDisplay != nil {
		return errDisplay
	}
	return errResp
}

// systemStartCmd is the struct representing the command to start system.
type systemStartCmd struct {
	logCmd
	ctlInvokerCmd
	jsonOutputCmd
	rankListCmd
}

// Execute is run when systemStartCmd activates
func (cmd *systemStartCmd) Execute(_ []string) error {
	hostSet, rankSet, err := cmd.validateHostsRanks()
	if err != nil {
		return err
	}
	req := new(control.SystemStartReq)
	req.Hosts.ReplaceSet(hostSet)
	req.Ranks.ReplaceSet(rankSet)

	resp, err := control.SystemStart(context.Background(), cmd.ctlInvoker, req)
	errCmd := errors.Wrap(err, "System-Start command")
	errResp := errors.Wrap(resp.Errors(), "System-Start command")

	if cmd.jsonOutputEnabled() {
		if errCmd != nil {
			return cmd.outputJSON(resp, errCmd)
		}

		return cmd.outputJSON(resp, errResp)
	}

	if errCmd != nil {
		return errCmd
	}

	cmd.log.Debugf("System-Start command: absent hosts: %s, absent ranks: %s",
		resp.AbsentHosts.String(), resp.AbsentRanks.String())

	if errDisplay := displaySystemAction(cmd.log, resp.Results,
		&resp.AbsentHosts, &resp.AbsentRanks); errDisplay != nil {
		return errDisplay
	}

	return errResp
}

// systemListPoolsCmd represents the command to fetch a list of all DAOS pools in the system.
type systemListPoolsCmd struct {
	logCmd
	cfgCmd
	ctlInvokerCmd
	jsonOutputCmd
}

// Execute is run when systemListPoolsCmd activates
func (cmd *systemListPoolsCmd) Execute(_ []string) error {
	if cmd.config == nil {
		return errors.New("No configuration loaded")
	}

	ctx := context.Background()
	req := new(control.ListPoolsReq)
	req.SetSystem(cmd.config.SystemName)
	resp, err := control.ListPools(ctx, cmd.ctlInvoker, req)

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, err)
	}

	if err != nil {
		return errors.Wrap(err, "List-Pools command failed")
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
			row[svcRepTitle] = pretty.FormatRanks(pool.SvcReplicas)
		}

		table = append(table, row)
	}

	cmd.log.Info(formatter.Format(table))
	return nil
}
