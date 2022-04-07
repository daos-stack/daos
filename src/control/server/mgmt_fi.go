//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build fault_injection
// +build fault_injection

package server

import (
	"context"

	chkpb "github.com/daos-stack/daos/src/control/common/proto/chk"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/system/checker"
)

func (svc *mgmtSvc) FaultInjectReport(ctx context.Context, rpt *chkpb.CheckReport) (resp *mgmtpb.DaosResp, err error) {
	if err := svc.checkLeaderRequest(rpt); err != nil {
		return nil, err
	}

	defer func() {
		svc.log.Debugf("Responding to FaultInjectReport RPC: %s (%+v)", mgmtpb.Debug(resp), err)
	}()
	svc.log.Debugf("Received FaultInjectReport RPC: %+v", rpt)

	cf := checker.NewFinding(rpt)
	if err := svc.sysdb.AddCheckerFinding(cf); err != nil {
		return nil, err
	}

	return new(mgmtpb.DaosResp), nil
}
