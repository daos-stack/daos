//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"fmt"
	"io"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
	"github.com/daos-stack/daos/src/control/lib/ui"
	"github.com/daos-stack/daos/src/control/system"
)

// SystemCmd is the struct representing the top-level system subcommand.
type SystemCmd struct {
	LeaderQuery leaderQueryCmd   `command:"leader-query" description:"Query for current Management Service leader"`
	Query       systemQueryCmd   `command:"query" description:"Query DAOS system status"`
	Stop        systemStopCmd    `command:"stop" description:"Perform controlled shutdown of DAOS system"`
	Start       systemStartCmd   `command:"start" description:"Perform start of stopped DAOS system"`
	Erase       systemEraseCmd   `command:"erase" description:"Erase system metadata prior to reformat"`
	ListPools   PoolListCmd      `command:"list-pools" description:"List all pools in the DAOS system"`
	Cleanup     systemCleanupCmd `command:"cleanup" description:"Clean up all resources associated with the specified machine"`
	SetAttr     systemSetAttrCmd `command:"set-attr" description:"Set system attributes"`
	GetAttr     systemGetAttrCmd `command:"get-attr" description:"Get system attributes"`
	DelAttr     systemDelAttrCmd `command:"del-attr" description:"Delete system attributes"`
}

type leaderQueryCmd struct {
	baseCmd
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

	resp, err := control.LeaderQuery(ctx, cmd.ctlInvoker, req)
	if err != nil {
		return err // control api returned an error, disregard response
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, err)
	}

	cmd.Infof("Current Leader: %s\n   Replica Set: %s\n", resp.Leader,
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
	baseCmd
	cfgCmd
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
	req.Hosts.Replace(hostSet)
	req.Ranks.Replace(rankSet)

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
	cmd.Info(out.String())
	if outErr.String() != "" {
		cmd.Error(outErr.String())
	}

	return resp.Errors()
}

type systemEraseCmd struct {
	baseCmd
	ctlInvokerCmd
}

func (cmd *systemEraseCmd) Execute(_ []string) error {
	resp, err := control.SystemErase(context.Background(), cmd.ctlInvoker, new(control.SystemEraseReq))
	if err != nil {
		return err
	}

	return resp.Errors()
}

// systemStopCmd is the struct representing the command to shutdown DAOS system.
type systemStopCmd struct {
	baseCmd
	cfgCmd
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
	req := &control.SystemStopReq{Force: cmd.Force}
	req.Hosts.Replace(hostSet)
	req.Ranks.Replace(rankSet)

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
	cmd.Info(out.String())
	if outErr.String() != "" {
		cmd.Error(outErr.String())
	}

	return resp.Errors()
}

// systemStartCmd is the struct representing the command to start system.
type systemStartCmd struct {
	baseCmd
	cfgCmd
	ctlInvokerCmd
	jsonOutputCmd
	rankListCmd

	Checker bool `short:"c" long:"checker" description:"Start DAOS system in checker mode"`
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

	if cmd.Checker && (hostSet.Count() > 0 || rankSet.Count() > 0) {
		return errors.New("--check option cannot be used with --ranks or --rank-hosts options")
	}

	req := new(control.SystemStartReq)
	req.Checker = cmd.Checker
	req.Hosts = hostSet.String()
	req.Ranks = rankSet.String()

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
	cmd.Info(out.String())
	if outErr.String() != "" {
		cmd.Error(outErr.String())
	}

	return resp.Errors()
}

type systemCleanupCmd struct {
	baseCmd
	cfgCmd
	ctlInvokerCmd
	jsonOutputCmd

	Args struct {
		Machine string `positional-arg-name:"<Machine to cleanup>"`
	} `positional-args:"yes"`

	Verbose bool `long:"verbose" short:"v" description:"Output additional cleanup information"`
}

func (cmd *systemCleanupCmd) Execute(_ []string) (errOut error) {
	defer func() {
		errOut = errors.Wrap(errOut, "system cleanup failed")
	}()

	ctx := context.Background()
	req := new(control.SystemCleanupReq)
	req.SetSystem(cmd.config.SystemName)
	req.Machine = cmd.Args.Machine

	resp, err := control.SystemCleanup(ctx, cmd.ctlInvoker, req)
	if err != nil {
		return err // control api returned an error, disregard response
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, err)
	}

	var out, outErr strings.Builder
	if err := pretty.PrintSystemCleanupResponse(&out, &outErr, resp, cmd.Verbose); err != nil {
		return err
	}
	if outErr.String() != "" {
		cmd.Error(outErr.String())
	}

	// Infof prints raw string and doesn't try to expand "%"
	// preserving column formatting in txtfmt table
	cmd.Infof("%s", out.String())

	return resp.Errors()
}

// systemSetAttrCmd represents the command to set system properties.
type systemSetAttrCmd struct {
	baseCmd
	cfgCmd
	ctlInvokerCmd
	jsonOutputCmd

	Args struct {
		Attrs ui.SetPropertiesFlag `positional-arg-name:"system attributes to set (key:val[,key:val...])" required:"1"`
	} `positional-args:"yes"`
}

// Execute is run when systemSetAttrCmd subcommand is activated.
func (cmd *systemSetAttrCmd) Execute(_ []string) error {
	req := &control.SystemSetAttrReq{
		Attributes: cmd.Args.Attrs.ParsedProps,
	}

	err := control.SystemSetAttr(context.Background(), cmd.ctlInvoker, req)
	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(nil, err)
	}

	if err != nil {
		return errors.Wrap(err, "system set-attr failed")
	}
	cmd.Info("system set-attr succeeded")

	return nil
}

// systemGetAttrCmd represents the command to get system properties.
type systemGetAttrCmd struct {
	baseCmd
	cfgCmd
	ctlInvokerCmd
	jsonOutputCmd

	Args struct {
		Attrs ui.GetPropertiesFlag `positional-arg-name:"system attributes to get (key[,key...])"`
	} `positional-args:"yes"`
}

func prettyPrintAttrs(out io.Writer, props map[string]string) {
	if len(props) == 0 {
		fmt.Fprintln(out, "No system attributes found.")
		return
	}

	nameTitle := "Name"
	valueTitle := "Value"
	table := []txtfmt.TableRow{}
	for key, val := range props {
		row := txtfmt.TableRow{}
		row[nameTitle] = key
		row[valueTitle] = val
		table = append(table, row)
	}

	tf := txtfmt.NewTableFormatter(nameTitle, valueTitle)
	tf.InitWriter(out)
	tf.Format(table)
}

// Execute is run when systemGetAttrCmd subcommand is activated.
func (cmd *systemGetAttrCmd) Execute(_ []string) error {
	req := &control.SystemGetAttrReq{
		Keys: cmd.Args.Attrs.ParsedProps.ToSlice(),
	}

	resp, err := control.SystemGetAttr(context.Background(), cmd.ctlInvoker, req)
	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, err)
	}

	if err != nil {
		return errors.Wrap(err, "system get-attr failed")
	}

	var bld strings.Builder
	prettyPrintAttrs(&bld, resp.Attributes)
	cmd.Infof("%s", bld.String())

	return nil
}

// systemDelAttrCmd represents the command to delete system properties.
type systemDelAttrCmd struct {
	baseCmd
	cfgCmd
	ctlInvokerCmd
	jsonOutputCmd

	Args struct {
		Attrs ui.GetPropertiesFlag `positional-arg-name:"system attributes to delete (key[,key...])" required:"1"`
	} `positional-args:"yes"`
}

// Execute is run when systemDelAttrCmd subcommand is activated.
func (cmd *systemDelAttrCmd) Execute(_ []string) error {
	req := &control.SystemSetAttrReq{
		Attributes: make(map[string]string),
	}
	for _, key := range cmd.Args.Attrs.ParsedProps.ToSlice() {
		req.Attributes[key] = ""
	}

	err := control.SystemSetAttr(context.Background(), cmd.ctlInvoker, req)
	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(nil, err)
	}

	if err != nil {
		return errors.Wrap(err, "system del-attr failed")
	}
	cmd.Info("system del-attr succeeded")

	return nil
}
