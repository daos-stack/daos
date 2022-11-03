//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"bytes"
	"context"
	"fmt"
	"strconv"
	"strings"

	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/ui"
)

type checkCmdRoot struct {
	Enable    checkEnableCmd    `command:"enable" description:"Enable system checker"`
	Disable   checkDisableCmd   `command:"disable" description:"Disable system checker"`
	Start     checkStartCmd     `command:"start" description:"Start a system check"`
	Stop      checkStopCmd      `command:"stop" description:"Stop a system check"`
	Query     checkQueryCmd     `command:"query" description:"Query a system check"`
	SetPolicy checkSetPolicyCmd `command:"set-policy" description:"Set system checker policies"`
	GetPolicy checkGetPolicyCmd `command:"get-policy" description:"Get system checker policies"`
	Repair    checkRepairCmd    `command:"repair" description:"Repair a reported system check problem"`
}

type poolIDSet []PoolID

func (p poolIDSet) List() (ids []string) {
	ids = make([]string, len(p))

	for i, id := range p {
		ids[i] = id.String()
	}

	return
}

type checkCmdBase struct {
	cmdutil.LogCmd
	cfgCmd
	ctlInvokerCmd
	jsonOutputCmd
}

func (c *checkCmdBase) Execute(_ []string) error {
	return errors.New("not implemented")
}

type checkPoolCmdBase struct {
	checkCmdBase

	Args struct {
		Pools poolIDSet `positional-arg-name:"[pool name or UUID [pool name or UUID]] "`
	} `positional-args:"yes"`
}

type checkEnableCmd struct {
	checkCmdBase
}

func (cmd *checkEnableCmd) Execute([]string) error {
	req := new(control.SystemCheckEnableReq)
	return control.SystemCheckEnable(context.Background(), cmd.ctlInvoker, req)
}

type checkDisableCmd struct {
	checkCmdBase
}

func (cmd *checkDisableCmd) Execute([]string) error {
	req := new(control.SystemCheckDisableReq)
	return control.SystemCheckDisable(context.Background(), cmd.ctlInvoker, req)
}

type setRepPolFlag struct {
	ui.SetPropertiesFlag

	SetPolicies []*control.SystemCheckPolicy
}

func (f *setRepPolFlag) UnmarshalFlag(fv string) error {
	var keys []string
	for _, class := range control.CheckerPolicyClasses() {
		keys = append(keys, class.String())
	}
	f.SettableKeys(keys...)

	if err := f.SetPropertiesFlag.UnmarshalFlag(fv); err != nil {
		return err
	}

	f.SetPolicies = make([]*control.SystemCheckPolicy, 0, len(f.ParsedProps))
	for class, action := range f.ParsedProps {
		policy, err := control.NewSystemCheckPolicy(class, action)
		if err != nil {
			return err
		}
		f.SetPolicies = append(f.SetPolicies, policy)
	}

	return nil
}

func (f *setRepPolFlag) Complete(match string) []flags.Completion {
	actions := control.CheckerPolicyActions()
	actKeys := make([]string, len(actions))
	for i, act := range actions {
		actKeys[i] = act.String()
	}
	comps := make(ui.CompletionMap)
	for _, class := range control.CheckerPolicyClasses() {
		comps[class.String()] = actKeys
	}
	f.SetCompletions(comps)

	return f.SetPropertiesFlag.Complete(match)
}

type checkStartCmd struct {
	checkPoolCmdBase

	DryRun   bool          `short:"n" long:"dry-run" description:"Scan only; do not initiate repairs."`
	Reset    bool          `short:"r" long:"reset" description:"Reset the system check state."`
	Failout  bool          `short:"f" long:"failout" description:"Stop on failure."`
	Auto     bool          `short:"a" long:"auto" description:"Attempt to automatically repair problems."`
	Policies setRepPolFlag `short:"p" long:"policies" description:"Set repair policies."`
}

func (cmd *checkStartCmd) Execute(_ []string) error {
	ctx := context.Background()

	req := new(control.SystemCheckStartReq)
	req.Uuids = cmd.Args.Pools.List()

	if cmd.DryRun {
		req.Flags |= uint32(control.SystemCheckFlagDryRun)
	}
	if cmd.Reset {
		req.Flags |= uint32(control.SystemCheckFlagReset)
	}
	if cmd.Failout {
		req.Flags |= uint32(control.SystemCheckFlagFailout)
	}
	if cmd.Auto {
		req.Flags |= uint32(control.SystemCheckFlagAuto)
	}
	req.Policies = cmd.Policies.SetPolicies

	if err := control.SystemCheckStart(ctx, cmd.ctlInvoker, req); err != nil {
		return err
	}

	cmd.Info("system checker started")

	return nil
}

type checkStopCmd struct {
	checkPoolCmdBase
}

func (cmd *checkStopCmd) Execute(_ []string) error {
	ctx := context.Background()

	req := new(control.SystemCheckStopReq)
	req.Uuids = cmd.Args.Pools.List()

	if err := control.SystemCheckStop(ctx, cmd.ctlInvoker, req); err != nil {
		return err
	}

	cmd.Info("system checker stopped")

	return nil
}

type checkQueryCmd struct {
	checkPoolCmdBase
}

func (cmd *checkQueryCmd) Execute(_ []string) error {
	ctx := context.Background()

	req := new(control.SystemCheckQueryReq)
	req.Uuids = cmd.Args.Pools.List()

	resp, err := control.SystemCheckQuery(ctx, cmd.ctlInvoker, req)
	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, err)
	}
	if err != nil {
		return err
	}

	var buf bytes.Buffer
	pretty.PrintCheckQueryResp(&buf, resp)
	cmd.Info(buf.String())

	return nil
}

type checkSetPolicyCmd struct {
	checkCmdBase

	Args struct {
		Policies setRepPolFlag `description:"Repair policies" required:"1"`
	} `positional-args:"yes"`
}

func (cmd *checkSetPolicyCmd) Execute(_ []string) error {
	ctx := context.Background()

	req := &control.SystemCheckSetPolicyReq{
		Policies: cmd.Args.Policies.SetPolicies,
	}
	err := control.SystemCheckSetPolicy(ctx, cmd.ctlInvoker, req)
	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(nil, err)
	}
	if err != nil {
		return err
	}

	cmd.Info("system checker policies updated")

	return nil
}

type getRepPolFlag struct {
	ui.GetPropertiesFlag

	ReqClasses []control.SystemCheckFindingClass
}

func (f *getRepPolFlag) UnmarshalFlag(fv string) error {
	var keys []string
	for _, class := range control.CheckerPolicyClasses() {
		keys = append(keys, class.String())
	}
	f.GettableKeys(keys...)

	if err := f.GetPropertiesFlag.UnmarshalFlag(fv); err != nil {
		return err
	}

	i := 0
	f.ReqClasses = make([]control.SystemCheckFindingClass, len(f.ParsedProps))
	for class := range f.ParsedProps {
		if err := f.ReqClasses[i].FromString(class); err != nil {
			return err
		}
		i++
	}

	return nil
}

func (f *getRepPolFlag) Complete(match string) []flags.Completion {
	comps := make(ui.CompletionMap)
	for _, class := range control.CheckerPolicyClasses() {
		comps[class.String()] = nil
	}
	f.SetCompletions(comps)

	return f.GetPropertiesFlag.Complete(match)
}

type checkGetPolicyCmd struct {
	checkCmdBase

	Args struct {
		Classes getRepPolFlag `description:"Inconsistency class names"`
	} `positional-args:"yes"`
}

func (cmd *checkGetPolicyCmd) Execute(_ []string) error {
	ctx := context.Background()

	req := new(control.SystemCheckGetPolicyReq)
	req.SetClasses(cmd.Args.Classes.ReqClasses)
	resp, err := control.SystemCheckGetPolicy(ctx, cmd.ctlInvoker, req)
	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, err)
	}
	if err != nil {
		return err
	}

	var buf bytes.Buffer
	pretty.PrintCheckerPolicies(&buf, resp.CheckerFlags, resp.Policies...)
	cmd.Info(buf.String())

	return nil
}

type repairSeqNum uint64

func (r repairSeqNum) String() string {
	return fmt.Sprintf("0x%x", uint64(r))
}

func (r *repairSeqNum) UnmarshalFlag(value string) error {
	var val uint64
	var err error
	if strings.HasPrefix(value, "0x") {
		cleaned := strings.Replace(value, "0x", "", -1)
		val, err = strconv.ParseUint(cleaned, 16, 64)
	} else {
		val, err = strconv.ParseUint(value, 10, 64)
	}

	if err != nil {
		return err
	}

	*r = repairSeqNum(val)
	return nil
}

type checkRepairCmd struct {
	checkCmdBase

	ForAll bool `short:"f" long:"for-all" description:"Take the same action for all inconsistencies with the same class."`

	Args struct {
		SeqNum         repairSeqNum `positional-arg-name:"[seq-num]" required:"1"`
		SelectedAction int          `positional-arg-name:"[action]" required:"1"`
	} `positional-args:"yes"`
}

func (cmd *checkRepairCmd) Execute(_ []string) error {
	ctx := context.Background()

	qReq := new(control.SystemCheckQueryReq)
	qReq.Seqs = []uint64{uint64(cmd.Args.SeqNum)}
	qResp, err := control.SystemCheckQuery(ctx, cmd.ctlInvoker, qReq)
	if err != nil {
		return err
	}

	if len(qResp.Reports) == 0 {
		return errors.Errorf("no report found for seq %s", cmd.Args.SeqNum)
	}

	report := qResp.Reports[0]
	if !report.IsInteractive() {
		return errors.Errorf("finding %s is already resolved: %s", cmd.Args.SeqNum, report.Resolution())
	}
	choices := report.RepairChoices()
	if cmd.Args.SelectedAction < 0 || cmd.Args.SelectedAction >= len(choices) {
		return errors.Errorf("invalid action %d for seq %s", cmd.Args.SelectedAction, cmd.Args.SeqNum)
	}

	req := new(control.SystemCheckRepairReq)
	req.Seq = uint64(cmd.Args.SeqNum)
	req.ForAll = cmd.ForAll
	if err := req.SetAction(int32(choices[cmd.Args.SelectedAction].Action)); err != nil {
		return err
	}

	if err := control.SystemCheckRepair(ctx, cmd.ctlInvoker, req); err != nil {
		return err
	}

	cmd.Info("Repair request sent")

	return nil
}
