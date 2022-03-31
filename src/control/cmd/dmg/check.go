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

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
)

type checkCmdRoot struct {
	Start checkStartCmd `command:"start" description:"Start a system check"`
	Stop  checkStopCmd  `command:"stop" description:"Stop a system check"`
	Query checkQueryCmd `command:"query" description:"Query a system check"`
	Prop  checkPropCmd  `command:"prop" description:"Get system check properties"`
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
	logCmd
	cfgCmd
	ctlInvokerCmd
	jsonOutputCmd

	Args struct {
		Pools poolIDSet `positional-arg-name:"[pool name or UUID [pool name or UUID]] "`
	} `positional-args:"yes"`
}

func (c *checkCmdBase) Execute(_ []string) error {
	return errors.New("not implemented")
}

type checkStartCmd struct {
	checkCmdBase

	DryRun  bool `short:"n" long:"dry-run" description:"Scan only; do not initiate repairs."`
	Reset   bool `short:"r" long:"reset" description:"Reset the system check state."`
	Failout bool `short:"f" long:"failout" description:"Stop on failure."`
	Auto    bool `short:"a" long:"auto" description:"Attempt to automatically repair problems."`
}

func (cmd *checkStartCmd) Execute(_ []string) error {
	ctx := context.Background()

	req := new(control.SystemCheckStartReq)
	req.Uuids = cmd.Args.Pools.List()

	if cmd.DryRun {
		req.Flags |= control.SystemCheckFlagDryRun
	}
	if cmd.Reset {
		req.Flags |= control.SystemCheckFlagReset
	}
	if cmd.Failout {
		req.Flags |= control.SystemCheckFlagFailout
	}
	if cmd.Auto {
		req.Flags |= control.SystemCheckFlagAuto
	}

	if err := control.SystemCheckStart(ctx, cmd.ctlInvoker, req); err != nil {
		return err
	}

	cmd.log.Info("system checker started")

	return nil
}

type checkStopCmd struct {
	checkCmdBase
}

func (cmd *checkStopCmd) Execute(_ []string) error {
	ctx := context.Background()

	req := new(control.SystemCheckStopReq)
	req.Uuids = cmd.Args.Pools.List()

	if err := control.SystemCheckStop(ctx, cmd.ctlInvoker, req); err != nil {
		return err
	}

	cmd.log.Info("system checker stopped")

	return nil
}

type checkQueryCmd struct {
	checkCmdBase
}

func (cmd *checkCmdBase) printQueryResp(resp *control.SystemCheckQueryResp) {
	cmd.log.Infof("Current phase: %s", resp.InsPhase)

	if len(resp.Reports) == 0 {
		cmd.log.Infof("No reports to display")
		return
	}

	var buf bytes.Buffer
	iw := txtfmt.NewIndentWriter(&buf)
	cmd.log.Info("Inconsistency Reports:")
	for _, report := range resp.Reports {
		fmt.Fprintf(iw, "0x%x %s: %s\n", report.Seq, report.Class, report.Msg)
		if len(report.Actions) == 0 {
			continue
		}
		fmt.Fprintf(iw, "Potential resolution actions:\n")
		iw2 := txtfmt.NewIndentWriter(iw)
		for i, action := range report.Actions {
			fmt.Fprintf(iw2, "%d: %s\n", action, report.Details[i])
		}
		fmt.Fprintln(&buf)
	}
	cmd.log.Info(buf.String())
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

	cmd.printQueryResp(resp)

	return nil
}

type checkPropCmd struct {
	checkCmdBase
}

func (cmd *checkPropCmd) Execute(_ []string) error {
	ctx := context.Background()

	req := new(control.SystemCheckPropReq)
	resp, err := control.SystemCheckProp(ctx, cmd.ctlInvoker, req)
	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, err)
	}
	if err != nil {
		return err
	}

	cmd.log.Infof("System check properties: %s\n", "TODO")

	return nil
}
