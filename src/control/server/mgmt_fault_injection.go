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

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/system/checker"
)

func (svc *mgmtSvc) FaultsAddFinding(ctx context.Context, req *mgmtpb.FaultsAddFindingReq) (*mgmtpb.FaultsAddFindingResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("Received FaultsAddFinding RPC: %+v", req)

	cf := checker.NewInjectedFinding(req.Finding)
	if err := svc.sysdb.AddCheckerFinding(cf); err != nil {
		return nil, err
	}

	resp := &mgmtpb.FaultsAddFindingResp{}

	svc.log.Debugf("Responding to FaultsAddFinding RPC: %s", mgmtpb.Debug(resp))

	return resp, nil
}
