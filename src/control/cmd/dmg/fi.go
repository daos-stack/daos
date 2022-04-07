//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build fault_injection
// +build fault_injection

package main

import (
	"context"
	"encoding/json"
	"io/ioutil"
	"math/rand"
	"strconv"
	"strings"
	"time"

	"github.com/google/uuid"
	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common/cmdutil"
	chkpb "github.com/daos-stack/daos/src/control/common/proto/chk"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/system/checker"
)

type faultsCmdRoot struct {
	Faults faultCmd `command:"faults" description:"Inject system fault"`
}

type faultCmd struct {
	AddCheckerReport addCheckerReportCmd `command:"add-checker-report" description:"Add a system checker report"`
}

type chkRptCls chkpb.CheckInconsistClass

func (c chkRptCls) String() string {
	return chkpb.CheckInconsistClass_name[int32(c)]
}

func (c chkRptCls) Complete(match string) (comps []flags.Completion) {
	for _, v := range chkpb.CheckInconsistClass_name {
		if strings.HasPrefix(v, match) {
			comps = append(comps, flags.Completion{Item: v})
		}
	}
	return
}

func (c *chkRptCls) UnmarshalFlag(value string) error {
	for i, v := range chkpb.CheckInconsistClass_name {
		if v == value {
			*c = chkRptCls(i)
			return nil
		}
	}

	if v, err := strconv.Atoi(value); err == nil {
		if _, found := chkpb.CheckInconsistClass_name[int32(v)]; found {
			*c = chkRptCls(v)
			return nil
		}
	}
	return errors.Errorf("invalid class %s", value)
}

type addCheckerReportCmd struct {
	cmdutil.LogCmd
	cfgCmd
	ctlInvokerCmd
	jsonOutputCmd

	File  string    `short:"f" long:"file" description:"File containing checker report in JSON format"`
	Class chkRptCls `short:"c" long:"class" description:"Checker report class (canned reports)"`
}

func (cmd *addCheckerReportCmd) Execute(_ []string) (errOut error) {
	defer func() {
		errOut = errors.Wrap(errOut, "add checker finding")
	}()

	var rpt *chkpb.CheckReport
	if cmd.File != "" {
		buf, err := ioutil.ReadFile(cmd.File)
		if err != nil {
			return errors.Wrapf(err, "failed to open file %s", cmd.File)
		}
		rpt = new(chkpb.CheckReport)
		if err := json.Unmarshal(buf, rpt); err != nil {
			return errors.Wrapf(err, "failed to parse file %s", cmd.File)
		}
	} else {
		rand.Seed(time.Now().UnixNano())

		cls := chkpb.CheckInconsistClass(cmd.Class)
		// Define some canned reports based on class. These can be used
		// for prototyping and testing. For more control, define a report
		// in JSON format and load it with the --file option.
		switch cls {
		case chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL:
			rpt = &chkpb.CheckReport{
				Seq:      rand.Uint64(),
				Class:    cls,
				Action:   chkpb.CheckInconsistAction_CIA_INTERACT,
				PoolUuid: uuid.New().String(),
				ActChoices: []chkpb.CheckInconsistAction{
					chkpb.CheckInconsistAction_CIA_TRUST_MS,
					chkpb.CheckInconsistAction_CIA_TRUST_PS,
					chkpb.CheckInconsistAction_CIA_IGNORE,
				},
				ActDetails: []string{"ms-label", "ps-label"},
			}
		default:
			return errors.Errorf("no canned report for class: %s", cls)
		}

		// For canned reports, annotate the report for nice messages.
		// For reports loaded from file, don't annotate them, just use them as-is.
		f := checker.AnnotateFinding(checker.NewFinding(rpt))
		rpt = &f.CheckReport
	}

	if rpt.Class == chkpb.CheckInconsistClass_CIC_NONE {
		return errors.New("class must be set")
	}

	ctx := context.Background()
	resp, err := control.InvokeFaultRPC(ctx, cmd.ctlInvoker,
		func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
			cmd.Debugf("injecting checker report: %+v", rpt)
			return mgmtpb.NewMgmtSvcClient(conn).FaultInjectReport(ctx, rpt)
		},
	)

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, err)
	}

	if err != nil {
		return err
	}

	cmd.Info("Checker report added")

	return nil
}
