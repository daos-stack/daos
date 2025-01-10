//
// (C) Copyright 2019-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build fault_injection
// +build fault_injection

package main

import (
	"context"
	"encoding/json"
	"math/rand"
	"os"
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
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/lib/ui"
	"github.com/daos-stack/daos/src/control/system/checker"
)

type faultsCmdRoot struct {
	Faults faultCmd `command:"faults" description:"Inject system fault"`
}

type faultCmd struct {
	AddCheckerReport addCheckerReportCmd `command:"add-checker-report" description:"Add a system checker report"`
	MgmtSvcFault     mgmtSvcFaultCmd     `command:"mgmt-svc" alias:"ms" description:"Inject management service fault"`
	PoolSvcFault     poolSvcFaultCmd     `command:"pool-svc" alias:"ps" description:"Inject pool service fault"`
}

type chkRptCls struct {
	class control.SystemCheckFindingClass
}

func (c chkRptCls) Complete(match string) (comps []flags.Completion) {
	for _, cls := range control.CheckerPolicyClasses() {
		if strings.HasPrefix(cls.String(), match) {
			comps = append(comps, flags.Completion{Item: cls.String()})
		}
	}
	return
}

func (c *chkRptCls) UnmarshalFlag(value string) error {
	return c.class.FromString(value)
}

func (c chkRptCls) ToProto() chkpb.CheckInconsistClass {
	return chkpb.CheckInconsistClass(c.class)
}

type addCheckerReportCmd struct {
	cmdutil.JSONOutputCmd
	baseCmd
	ctlInvokerCmd

	File  string    `short:"f" long:"file" description:"File containing checker report in JSON format"`
	Class chkRptCls `short:"c" long:"class" description:"Checker report class (canned reports)"`
}

func (cmd *addCheckerReportCmd) Execute(_ []string) (errOut error) {
	defer func() {
		errOut = errors.Wrap(errOut, "add checker finding")
	}()

	var rpt *chkpb.CheckReport
	if cmd.File != "" {
		buf, err := os.ReadFile(cmd.File)
		if err != nil {
			return errors.Wrapf(err, "failed to open file %s", cmd.File)
		}
		rpt = new(chkpb.CheckReport)
		if err := json.Unmarshal(buf, rpt); err != nil {
			return errors.Wrapf(err, "failed to parse file %s", cmd.File)
		}
	} else {
		rand.Seed(time.Now().UnixNano())

		cls := cmd.Class.ToProto()
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

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, nil)
	}

	if err != nil {
		return err
	}

	cmd.Info("Checker report added")

	return nil
}

type labelFlag struct {
	ui.LabelOrUUIDFlag
}

func (f *labelFlag) UnmarshalFlag(value string) error {
	if err := f.LabelOrUUIDFlag.UnmarshalFlag(value); err != nil {
		return errors.Wrap(err, "invalid label")
	}

	if f.HasUUID() {
		return errors.New("UUID is not a valid input")
	}

	return nil
}

type poolFaultCmd struct {
	poolCmd

	SvcList ui.RankSetFlag `short:"s" long:"svcl" description:"List of pool service ranks"`
	Label   labelFlag      `short:"l" long:"label" description:"Pool service label"`

	Args struct {
		Class chkRptCls `positional-arg-name:"<chk rpt class>" description:"Checker report class" required:"1"`
	} `positional-args:"yes"`
}

type mgmtSvcFaultCmd struct {
	Pool mgmtSvcPoolFaultCmd `command:"pool" description:"Modify a pool service entry in the MS DB"`
}

type mgmtSvcPoolFaultCmd struct {
	poolFaultCmd
}

func (cmd *mgmtSvcPoolFaultCmd) Execute([]string) (errOut error) {
	resp, err := control.InvokeFaultRPC(context.Background(), cmd.ctlInvoker,
		func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
			return mgmtpb.NewMgmtSvcClient(conn).FaultInjectMgmtPoolFault(ctx,
				&chkpb.Fault{
					Class:   cmd.Args.Class.ToProto(),
					Strings: []string{cmd.PoolID().String(), cmd.Label.Label},
					Uints:   ranklist.RanksToUint32(cmd.SvcList.Ranks()),
				},
			)
		},
	)

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, nil)
	}

	if err != nil {
		return err
	}

	cmd.Info("Mgmt service fault injected")

	return nil
}

type poolSvcFaultCmd struct {
	poolFaultCmd
}

func (cmd *poolSvcFaultCmd) Execute([]string) (errOut error) {
	resp, err := control.InvokeFaultRPC(context.Background(), cmd.ctlInvoker,
		func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
			return mgmtpb.NewMgmtSvcClient(conn).FaultInjectPoolFault(ctx,
				&chkpb.Fault{
					Class:   cmd.Args.Class.ToProto(),
					Strings: []string{cmd.PoolID().String(), cmd.Label.Label},
					Uints:   ranklist.RanksToUint32(cmd.SvcList.Ranks()),
				},
			)
		},
	)

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, nil)
	}

	if err != nil {
		return err
	}

	cmd.Info("Pool service fault injected")

	return nil
}
