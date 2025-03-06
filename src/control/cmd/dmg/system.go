//
// (C) Copyright 2019-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"io"
	"strings"

	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
	"github.com/daos-stack/daos/src/control/lib/ui"
)

var errNoRanks = errors.New("no ranks or hosts specified")

// SystemCmd is the struct representing the top-level system subcommand.
type SystemCmd struct {
	LeaderQuery  leaderQueryCmd        `command:"leader-query" description:"Query for current Management Service leader"`
	Query        systemQueryCmd        `command:"query" description:"Query DAOS system status"`
	Stop         systemStopCmd         `command:"stop" description:"Perform controlled shutdown of DAOS system"`
	Start        systemStartCmd        `command:"start" description:"Perform start of stopped DAOS system"`
	Exclude      systemExcludeCmd      `command:"exclude" description:"Exclude ranks from DAOS system"`
	ClearExclude systemClearExcludeCmd `command:"clear-exclude" description:"Clear excluded state for ranks"`
	Drain        systemDrainCmd        `command:"drain" description:"Drain ranks or hosts from all relevant pools in DAOS system"`
	Reintegrate  systemReintegrateCmd  `command:"reintegrate" alias:"reint" description:"Reintegrate ranks or hosts into all relevant pools in DAOS system"`
	Erase        systemEraseCmd        `command:"erase" description:"Erase system metadata prior to reformat"`
	ListPools    poolListCmd           `command:"list-pools" description:"List all pools in the DAOS system"`
	Cleanup      systemCleanupCmd      `command:"cleanup" description:"Clean up all resources associated with the specified machine"`
	SetAttr      systemSetAttrCmd      `command:"set-attr" description:"Set system attributes"`
	GetAttr      systemGetAttrCmd      `command:"get-attr" description:"Get system attributes"`
	DelAttr      systemDelAttrCmd      `command:"del-attr" description:"Delete system attributes"`
	SetProp      systemSetPropCmd      `command:"set-prop" description:"Set system properties"`
	GetProp      systemGetPropCmd      `command:"get-prop" description:"Get system properties"`
}

type baseCtlCmd struct {
	baseCmd
	cfgCmd
	ctlInvokerCmd
	cmdutil.JSONOutputCmd
}

type leaderQueryCmd struct {
	baseCtlCmd
	DownReplicas bool `short:"N" long:"down-replicas" description:"Show Down Replicas only"`
}

func (cmd *leaderQueryCmd) Execute(_ []string) (errOut error) {
	defer func() {
		errOut = errors.Wrap(errOut, "leader query failed")
	}()

	if cmd.config == nil {
		return errors.New("no configuration loaded")
	}

	ctx := cmd.MustLogCtx()
	req := new(control.LeaderQueryReq)

	resp, err := control.LeaderQuery(ctx, cmd.ctlInvoker, req)
	if err != nil {
		return err // control api returned an error, disregard response
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, err)
	}

	if cmd.DownReplicas {
		cmd.Infof("Unresponsive Replicas: %s\n", strings.Join(resp.DownReplicas, ", "))
	} else {
		cmd.Infof("Current Leader: %s\n   Replica Set: %s\n  Unresponsive Replicas: %s\n", resp.Leader,
			strings.Join(resp.Replicas, ", "), strings.Join(resp.DownReplicas, ", "))
	}

	return nil
}

// rankListCmd enables rank or host list to be supplied with command to filter
// which ranks are operated upon.
type rankListCmd struct {
	Ranks ui.RankSetFlag `long:"ranks" short:"r" description:"Comma separated ranges or individual system ranks to operate on"`
	Hosts ui.HostSetFlag `long:"rank-hosts" description:"Hostlist representing hosts whose managed ranks are to be operated on"`
}

// validateHostsRanks validates rank and host lists have correct format.
//
// Populate request with valid list strings.
func (cmd *rankListCmd) validateHostsRanks() error {
	if cmd.Hosts.Count() > 0 && cmd.Ranks.Count() > 0 {
		return errors.New("--ranks and --rank-hosts options cannot be set together")
	}

	return nil
}

type baseRankListCmd struct {
	baseCtlCmd
	rankListCmd
}

// systemQueryCmd is the struct representing the command to query system status.
type systemQueryCmd struct {
	baseRankListCmd
	Verbose      bool                  `long:"verbose" short:"v" description:"Display more member details"`
	NotOK        bool                  `long:"not-ok" description:"Display components in need of administrative investigation"`
	WantedStates ui.MemberStateSetFlag `long:"with-states" description:"Only show engines in one of a set of comma-separated states"`
}

// Execute is run when systemQueryCmd activates.
func (cmd *systemQueryCmd) Execute(_ []string) (errOut error) {
	defer func() {
		errOut = errors.Wrap(errOut, "system query failed")
	}()

	if cmd.NotOK && !cmd.WantedStates.Empty() {
		return errors.New("--not-ok and --with-states options cannot be set together")
	}
	if err := cmd.validateHostsRanks(); err != nil {
		return err
	}
	req := new(control.SystemQueryReq)
	req.Hosts.Replace(&cmd.Hosts.HostSet)
	req.Ranks.Replace(&cmd.Ranks.RankSet)
	req.NotOK = cmd.NotOK
	req.WantedStates = cmd.WantedStates.States

	resp, err := control.SystemQuery(cmd.MustLogCtx(), cmd.ctlInvoker, req)
	if err != nil {
		return err // control api returned an error, disregard response
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, resp.Errors())
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
	resp, err := control.SystemErase(cmd.MustLogCtx(), cmd.ctlInvoker, new(control.SystemEraseReq))
	if err != nil {
		return err
	}

	return resp.Errors()
}

// systemStopCmd is the struct representing the command to shutdown DAOS system.
type systemStopCmd struct {
	baseRankListCmd
	Force bool `long:"force" description:"Force stop DAOS system members"`
}

// Execute is run when systemStopCmd activates.
//
// Always request both prep and kill stages when calling control API.
func (cmd *systemStopCmd) Execute(_ []string) (errOut error) {
	defer func() {
		errOut = errors.Wrap(errOut, "system stop failed")
	}()

	if err := cmd.validateHostsRanks(); err != nil {
		return err
	}
	req := &control.SystemStopReq{Force: cmd.Force}
	req.Hosts.Replace(&cmd.Hosts.HostSet)
	req.Ranks.Replace(&cmd.Ranks.RankSet)

	resp, err := control.SystemStop(cmd.MustLogCtx(), cmd.ctlInvoker, req)
	if err != nil {
		return err // control api returned an error, disregard response
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, resp.Errors())
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
	baseRankListCmd
}

// Execute is run when systemStartCmd activates.
func (cmd *systemStartCmd) Execute(_ []string) (errOut error) {
	defer func() {
		errOut = errors.Wrap(errOut, "system start failed")
	}()

	if err := cmd.validateHostsRanks(); err != nil {
		return err
	}

	req := new(control.SystemStartReq)
	req.Hosts.Replace(&cmd.Hosts.HostSet)
	req.Ranks.Replace(&cmd.Ranks.RankSet)

	resp, err := control.SystemStart(cmd.MustLogCtx(), cmd.ctlInvoker, req)
	if err != nil {
		return err // control api returned an error, disregard response
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, resp.Errors())
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

type systemExcludeCmd struct {
	baseRankListCmd
}

func (cmd *systemExcludeCmd) execute(clear bool) error {
	if err := cmd.validateHostsRanks(); err != nil {
		return err
	}
	if cmd.Ranks.Count() == 0 && cmd.Hosts.Count() == 0 {
		return errNoRanks
	}

	req := &control.SystemExcludeReq{Clear: clear}
	req.Hosts.Replace(&cmd.Hosts.HostSet)
	req.Ranks.Replace(&cmd.Ranks.RankSet)

	resp, err := control.SystemExclude(cmd.MustLogCtx(), cmd.ctlInvoker, req)
	if err != nil {
		return err // control api returned an error, disregard response
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, resp.Errors())
	}

	updated := ranklist.NewRankSet()
	for _, result := range resp.Results {
		updated.Add(result.Rank)
	}

	if resp.Errors() != nil {
		cmd.Errorf("Errors: %s", resp.Errors())
	}
	cmd.Infof("updated ranks: %s", updated)

	return resp.Errors()
}

func (cmd *systemExcludeCmd) Execute(_ []string) error {
	return cmd.execute(false)
}

type systemClearExcludeCmd struct {
	systemExcludeCmd
}

func (cmd *systemClearExcludeCmd) Execute(_ []string) error {
	return cmd.execute(true)
}

type systemDrainCmd struct {
	baseRankListCmd
}

func (cmd *systemDrainCmd) execute(reint bool) (errOut error) {
	defer func() {
		opStr := "drain"
		if reint {
			opStr = "reintegrate"
		}
		errOut = errors.Wrapf(errOut, "system %s failed", opStr)
	}()

	if err := cmd.validateHostsRanks(); err != nil {
		return err
	}
	if cmd.Ranks.Count() == 0 && cmd.Hosts.Count() == 0 {
		return errNoRanks
	}

	req := new(control.SystemDrainReq)
	req.SetSystem(cmd.config.SystemName)
	req.Hosts.Replace(&cmd.Hosts.HostSet)
	req.Ranks.Replace(&cmd.Ranks.RankSet)
	req.Reint = reint

	resp, err := control.SystemDrain(cmd.MustLogCtx(), cmd.ctlInvoker, req)
	if err != nil {
		return err // control api returned an error, disregard response
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, resp.Errors())
	}

	cmd.Debugf("%T: %+v, %T: %+v", req, req, resp.Responses, resp.Responses)

	var out strings.Builder
	pretty.PrintPoolRanksResps(&out, resp.Responses...)
	cmd.Info(out.String())

	return resp.Errors()
}

func (cmd *systemDrainCmd) Execute(_ []string) error {
	return cmd.execute(false)
}

type systemReintegrateCmd struct {
	systemDrainCmd
}

func (cmd *systemReintegrateCmd) Execute(_ []string) error {
	return cmd.execute(true)
}

type systemCleanupCmd struct {
	baseCtlCmd
	Args struct {
		Machine string `positional-arg-name:"machine to cleanup" required:"1"`
	} `positional-args:"yes"`
	Verbose bool `long:"verbose" short:"v" description:"Output additional cleanup information"`
}

func (cmd *systemCleanupCmd) Execute(_ []string) (errOut error) {
	defer func() {
		errOut = errors.Wrap(errOut, "system cleanup failed")
	}()

	req := new(control.SystemCleanupReq)
	req.SetSystem(cmd.config.SystemName)
	req.Machine = cmd.Args.Machine

	resp, err := control.SystemCleanup(cmd.MustLogCtx(), cmd.ctlInvoker, req)
	if err != nil {
		return err // control api returned an error, disregard response
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, resp.Errors())
	}

	var out strings.Builder
	pretty.PrintSystemCleanupResponse(&out, resp, cmd.Verbose)

	if resp.Errors() != nil {
		cmd.Error(resp.Errors().Error())
	}

	cmd.Info(out.String())

	return resp.Errors()
}

// systemSetAttrCmd represents the command to set system attributes.
type systemSetAttrCmd struct {
	baseCtlCmd
	Args struct {
		Attrs ui.SetPropertiesFlag `positional-arg-name:"system attributes to set (key:val[,key:val...])" required:"1"`
	} `positional-args:"yes"`
}

// Execute is run when systemSetAttrCmd subcommand is activated.
func (cmd *systemSetAttrCmd) Execute(_ []string) error {
	req := &control.SystemSetAttrReq{
		Attributes: cmd.Args.Attrs.ParsedProps,
	}

	err := control.SystemSetAttr(cmd.MustLogCtx(), cmd.ctlInvoker, req)
	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(nil, err)
	}

	if err != nil {
		return errors.Wrap(err, "system set-attr failed")
	}
	cmd.Info("system set-attr succeeded")

	return nil
}

// systemGetAttrCmd represents the command to get system attributes.
type systemGetAttrCmd struct {
	baseCtlCmd
	Args struct {
		Attrs ui.GetPropertiesFlag `positional-arg-name:"system attributes to get (key[,key...])"`
	} `positional-args:"yes"`
}

func prettyPrintAttrs(out io.Writer, attrs map[string]string) {
	if len(attrs) == 0 {
		fmt.Fprintln(out, "No system attributes found.")
		return
	}

	nameTitle := "Name"
	valueTitle := "Value"
	table := []txtfmt.TableRow{}
	for key, val := range attrs {
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

	resp, err := control.SystemGetAttr(cmd.MustLogCtx(), cmd.ctlInvoker, req)
	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, err)
	}

	if err != nil {
		return errors.Wrap(err, "system get-attr failed")
	}

	var bld strings.Builder
	prettyPrintAttrs(&bld, resp.Attributes)
	cmd.Infof("%s", bld.String())

	return nil
}

// systemDelAttrCmd represents the command to delete system attributes.
type systemDelAttrCmd struct {
	baseCtlCmd
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

	err := control.SystemSetAttr(cmd.MustLogCtx(), cmd.ctlInvoker, req)
	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(nil, err)
	}

	if err != nil {
		return errors.Wrap(err, "system del-attr failed")
	}
	cmd.Info("system del-attr succeeded")

	return nil
}

type systemSetPropsFlag struct {
	ui.SetPropertiesFlag
	SystemProps daos.SystemPropertyMap
	ParsedProps map[daos.SystemPropertyKey]daos.SystemPropertyValue
}

func (f *systemSetPropsFlag) UnmarshalFlag(fv string) error {
	err := f.SetPropertiesFlag.UnmarshalFlag(fv)
	if err != nil {
		return err
	}

	if f.SystemProps == nil {
		f.SystemProps = daos.SystemProperties()
	}
	f.ParsedProps = make(map[daos.SystemPropertyKey]daos.SystemPropertyValue)

	for k, v := range f.SetPropertiesFlag.ParsedProps {
		if prop, ok := f.SystemProps.Get(k); ok {
			if err := prop.Value.Handler(v); err != nil {
				return errors.Wrapf(err, "invalid system property value for %s", k)
			}
			f.ParsedProps[prop.Key] = prop.Value
		} else {
			return errors.Errorf("invalid system property key: %s", k)
		}
	}

	return nil
}

func (f *systemSetPropsFlag) Complete(match string) []flags.Completion {
	if f.SystemProps == nil {
		f.SystemProps = daos.SystemProperties()
	}

	comps := make(ui.CompletionMap)
	for _, prop := range f.SystemProps {
		comps[prop.Key.String()] = prop.Value.Choices()
	}
	f.SetCompletions(comps)

	return f.SetPropertiesFlag.Complete(match)
}

// systemSetPropCmd represents the command to set system properties.
type systemSetPropCmd struct {
	baseCtlCmd
	Args struct {
		Props systemSetPropsFlag `positional-arg-name:"system properties to set (key:val[,key:val...])" required:"1"`
	} `positional-args:"yes"`
}

// Execute is run when systemSetPropCmd subcommand is activated.
func (cmd *systemSetPropCmd) Execute(_ []string) error {
	req := &control.SystemSetPropReq{
		Properties: cmd.Args.Props.ParsedProps,
	}

	err := control.SystemSetProp(cmd.MustLogCtx(), cmd.ctlInvoker, req)
	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(nil, err)
	}

	if err != nil {
		return errors.Wrap(err, "system set-prop failed")
	}
	cmd.Info("system set-prop succeeded")

	return nil
}

type systemGetPropsFlag struct {
	ui.GetPropertiesFlag
	SystemProps daos.SystemPropertyMap
	ParsedProps []daos.SystemPropertyKey
}

func (f *systemGetPropsFlag) UnmarshalFlag(fv string) error {
	err := f.GetPropertiesFlag.UnmarshalFlag(fv)
	if err != nil {
		return err
	}

	if f.SystemProps == nil {
		f.SystemProps = daos.SystemProperties()
	}

	for _, k := range f.GetPropertiesFlag.ParsedProps.ToSlice() {
		if prop, ok := f.SystemProps.Get(k); ok {
			f.ParsedProps = append(f.ParsedProps, prop.Key)
		} else {
			return errors.Errorf("invalid system property key: %s", k)
		}
	}

	return nil
}

func (f *systemGetPropsFlag) Complete(match string) []flags.Completion {
	if f.SystemProps == nil {
		f.SystemProps = daos.SystemProperties()
	}

	comps := make(ui.CompletionMap)
	for _, prop := range f.SystemProps {
		comps[prop.Key.String()] = prop.Value.Choices()
	}
	f.SetCompletions(comps)

	return f.GetPropertiesFlag.Complete(match)
}

// systemGetPropCmd represents the command to get system properties.
type systemGetPropCmd struct {
	baseCtlCmd
	Args struct {
		Props systemGetPropsFlag `positional-arg-name:"system properties to get (key[,key...])"`
	} `positional-args:"yes"`
}

func prettyPrintSysProps(out io.Writer, props []*daos.SystemProperty) {
	if len(props) == 0 {
		fmt.Fprintln(out, "No system properties found.")
		return
	}

	nameTitle := "Name"
	valueTitle := "Value"
	table := []txtfmt.TableRow{}
	for _, prop := range props {
		row := txtfmt.TableRow{}
		row[nameTitle] = fmt.Sprintf("%s (%s)", prop.Description, prop.Key)
		row[valueTitle] = prop.Value.String()
		table = append(table, row)
	}

	tf := txtfmt.NewTableFormatter(nameTitle, valueTitle)
	tf.InitWriter(out)
	tf.Format(table)
}

// Execute is run when systemGetPropCmd subcommand is activated.
func (cmd *systemGetPropCmd) Execute(_ []string) error {
	req := &control.SystemGetPropReq{
		Keys: cmd.Args.Props.ParsedProps,
	}

	resp, err := control.SystemGetProp(cmd.MustLogCtx(), cmd.ctlInvoker, req)
	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp.Properties, err)
	}

	if err != nil {
		return errors.Wrap(err, "system get-attr failed")
	}

	var bld strings.Builder
	prettyPrintSysProps(&bld, resp.Properties)
	cmd.Infof("%s", bld.String())

	return nil
}
