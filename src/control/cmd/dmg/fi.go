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

	"github.com/pkg/errors"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/proto"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/control"
)

type faultsCmdRoot struct {
	Faults faultCmd `command:"faults" description:"Inject system fault"`
}

type faultCmd struct {
	AddCheckerFinding addCheckerFindingCmd `command:"add-checker-finding" description:"Add a system checker finding"`
}

type addCheckerFindingCmd struct {
	logCmd
	cfgCmd
	ctlInvokerCmd
	jsonOutputCmd

	ID          string   `short:"I" long:"id" description:"Checker finding ID"`
	Class       string   `short:"c" long:"class" description:"Class of finding" required:"1"`
	Status      string   `short:"s" long:"status" description:"Status of finding" required:"1"`
	Ignorable   bool     `short:"i" long:"ignorable" description:"Ignorable finding"`
	Description string   `short:"d" long:"description" description:"Description of finding" required:"1"`
	Resolutions []string `short:"r" long:"resolutions" description:"Resolutions of finding"`
}

func (cmd *addCheckerFindingCmd) Execute(_ []string) (errOut error) {
	defer func() {
		errOut = errors.Wrap(errOut, "add checker finding")
	}()

	convRes := func(resolutions ...string) []*mgmtpb.SystemCheckerResolution {
		var ret []*mgmtpb.SystemCheckerResolution
		for id, resolution := range resolutions {
			ret = append(ret, &mgmtpb.SystemCheckerResolution{
				Id:          uint32(id),
				Description: resolution,
			})
		}
		return ret
	}

	ctx := context.Background()
	resp, err := control.InvokeFaultRPC(ctx, cmd.ctlInvoker,
		func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
			return mgmtpb.NewMgmtSvcClient(conn).FaultsAddFinding(ctx, &mgmtpb.FaultsAddFindingReq{
				Sys: cmd.ctlInvoker.GetSystem(),
				Finding: &mgmtpb.SystemCheckerFinding{
					Id:          cmd.ID,
					Class:       cmd.Class,
					Status:      cmd.Status,
					Ignorable:   cmd.Ignorable,
					Description: cmd.Description,
					Resolutions: convRes(cmd.Resolutions...),
				},
			})
		},
	)

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, err)
	}

	if err != nil {
		return err
	}

	return nil
}
