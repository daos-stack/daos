//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

func (svc *mgmtSvc) getCheckerStatus(ctx context.Context) (*mgmtpb.SystemCheckerStatusResp, error) {
	status, err := svc.sysdb.GetCheckerState()
	if err != nil {
		return nil, err
	}

	findings, err := svc.sysdb.GetCheckerFindings()
	if err != nil {
		return nil, err
	}

	svc.log.Debugf("%d findings", len(findings))

	resp := &mgmtpb.SystemCheckerStatusResp{
		CurrentPass: status.CurrentPass.String(),
		Findings:    make([]*mgmtpb.SystemCheckerFinding, len(findings)),
	}
	for i, f := range findings {
		resp.Findings[i] = new(mgmtpb.SystemCheckerFinding)
		if err := convert.Types(f, resp.Findings[i]); err != nil {
			svc.log.Errorf("failed to convert %+v: %s", f, err)
			return nil, err
		}
	}

	return resp, nil
}

func (svc *mgmtSvc) SystemCheckerStart(ctx context.Context, req *mgmtpb.SystemCheckerStartReq) (resp *mgmtpb.SystemCheckerStatusResp, err error) {
	defer func() {
		svc.log.Debugf("Responding to SystemCheckerStart RPC: %s (%+v)", mgmtpb.Debug(resp), err)
	}()

	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("Received SystemCheckerStart RPC: %+v", req)

	if err := svc.sysChecker.RunCheckers(); err != nil {
		return nil, err
	}

	resp, err = svc.getCheckerStatus(ctx)
	if err != nil {
		return nil, err
	}

	return resp, nil
}

func (svc *mgmtSvc) SystemCheckerQuery(ctx context.Context, req *mgmtpb.SystemCheckerQueryReq) (resp *mgmtpb.SystemCheckerStatusResp, err error) {
	defer func() {
		svc.log.Debugf("Responding to SystemCheckerQuery RPC: %s (%+v)", mgmtpb.Debug(resp), err)
	}()

	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("Received SystemCheckerQuery RPC: %+v", req)

	resp, err = svc.getCheckerStatus(ctx)
	if err != nil {
		return nil, err
	}

	return resp, nil
}
