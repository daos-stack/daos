//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/system"
)

// SystemCmd is the struct representing the top-level system subcommand.
type SystemCmd struct {
	LeaderQuery leaderQueryCmd `command:"leader-query" alias:"l" description:"Query for current Management Service leader"`
	Query       systemQueryCmd `command:"query" alias:"q" description:"Query DAOS system status"`
	Stop        systemStopCmd  `command:"stop" alias:"s" description:"Perform controlled shutdown of DAOS system"`
	Start       systemStartCmd `command:"start" alias:"r" description:"Perform start of stopped DAOS system"`
	ListPools   PoolListCmd    `command:"list-pools" alias:"p" description:"List all pools in the DAOS system"`
}

type leaderQueryCmd struct {
	logCmd
	cfgCmd
	ctlInvokerCmd
	jsonOutputCmd
}

func (cmd *leaderQueryCmd) Execute(_ []string) (errOut error) {
	defer func() {
		errOut = errors.Wrap(errOut, "leader query failed")
	}()

	if cmd.config == nil {
		return errors.New("no configuration loaded")
	}

	ctx := context.Background()
	req := new(control.LeaderQueryReq)
	req.SetSystem(cmd.config.SystemName)

	resp, err := control.LeaderQuery(ctx, cmd.ctlInvoker, req)
	if err != nil {
		return err // control api returned an error, disregard response
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, err)
	}

	cmd.log.Infof("Current Leader: %s\n   Replica Set: %s\n", resp.Leader,
		strings.Join(resp.Replicas, ", "))

	return nil
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

// Execute is run when systemQueryCmd activates.
func (cmd *systemQueryCmd) Execute(_ []string) (errOut error) {
	defer func() {
		errOut = errors.Wrap(errOut, "system query failed")
	}()

	hostSet, rankSet, err := cmd.validateHostsRanks()
	if err != nil {
		return err
	}
	req := new(control.SystemQueryReq)
	req.Hosts.ReplaceSet(hostSet)
	req.Ranks.ReplaceSet(rankSet)

	resp, err := control.SystemQuery(context.Background(), cmd.ctlInvoker, req)
	if err != nil {
		return err // control api returned an error, disregard response
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, resp.Errors())
	}

	var out, outErr strings.Builder
	if err := pretty.PrintSystemQueryResponse(&out, &outErr, resp,
		pretty.PrintWithVerboseOutput(cmd.Verbose)); err != nil {
		return err
	}
	cmd.log.Info(out.String())
	if outErr.String() != "" {
		cmd.log.Error(outErr.String())
	}

	return resp.Errors()
}

// systemStopCmd is the struct representing the command to shutdown DAOS system.
type systemStopCmd struct {
	logCmd
	ctlInvokerCmd
	jsonOutputCmd
	rankListCmd
	Force bool `long:"force" description:"Force stop DAOS system members"`
}

// Execute is run when systemStopCmd activates.
//
// Always request both prep and kill stages when calling control API.
func (cmd *systemStopCmd) Execute(_ []string) (errOut error) {
	defer func() {
		errOut = errors.Wrap(errOut, "system stop failed")
	}()

	hostSet, rankSet, err := cmd.validateHostsRanks()
	if err != nil {
		return err
	}
	req := &control.SystemStopReq{Prep: true, Kill: true, Force: cmd.Force}
	req.Hosts.ReplaceSet(hostSet)
	req.Ranks.ReplaceSet(rankSet)

	resp, err := control.SystemStop(context.Background(), cmd.ctlInvoker, req)
	if err != nil {
		return err // control api returned an error, disregard response
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, resp.Errors())
	}

	var out, outErr strings.Builder
	if err := pretty.PrintSystemStopResponse(&out, &outErr, resp); err != nil {
		return err
	}
	cmd.log.Info(out.String())
	if outErr.String() != "" {
		cmd.log.Error(outErr.String())
	}

	return resp.Errors()
}

// systemStartCmd is the struct representing the command to start system.
type systemStartCmd struct {
	logCmd
	ctlInvokerCmd
	jsonOutputCmd
	rankListCmd
}

// Execute is run when systemStartCmd activates.
func (cmd *systemStartCmd) Execute(_ []string) (errOut error) {
	defer func() {
		errOut = errors.Wrap(errOut, "system start failed")
	}()

	hostSet, rankSet, err := cmd.validateHostsRanks()
	if err != nil {
		return err
	}
	req := new(control.SystemStartReq)
	req.Hosts.ReplaceSet(hostSet)
	req.Ranks.ReplaceSet(rankSet)

	resp, err := control.SystemStart(context.Background(), cmd.ctlInvoker, req)
	if err != nil {
		return err // control api returned an error, disregard response
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, resp.Errors())
	}

	var out, outErr strings.Builder
	if err := pretty.PrintSystemStartResponse(&out, &outErr, resp); err != nil {
		return err
	}
	cmd.log.Info(out.String())
	if outErr.String() != "" {
		cmd.log.Error(outErr.String())
	}

	return resp.Errors()
}
