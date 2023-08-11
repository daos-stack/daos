//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"encoding/json"
	"strings"

	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common"
	chkpb "github.com/daos-stack/daos/src/control/common/proto/chk"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/system"
	"github.com/daos-stack/daos/src/control/system/checker"
)

const (
	checkerEnabledKey  = "checker_enabled"
	checkerPoliciesKey = "checker_policies"
)

func (svc *mgmtSvc) enableChecker() error {
	if err := system.SetMgmtProperty(svc.sysdb, checkerEnabledKey, "true"); err != nil {
		return errors.Wrap(err, "failed to enable checker")
	}
	return nil
}

func (svc *mgmtSvc) disableChecker() error {
	if err := system.SetMgmtProperty(svc.sysdb, checkerEnabledKey, "false"); err != nil {
		return errors.Wrap(err, "failed to disable checker")
	}
	return nil
}

func (svc *mgmtSvc) checkerIsEnabled() bool {
	value, err := system.GetMgmtProperty(svc.sysdb, checkerEnabledKey)
	if err != nil {
		if !system.IsErrSystemAttrNotFound(err) {
			svc.log.Errorf("failed to get checker enabled value: %s", err)
		}
		return false
	}
	return value == "true"
}

// checkerRequest is a wrapper around a request that is made on behalf of
// the checker or is otherwise allowed to be made while the checker is enabled.
type checkerRequest struct {
	proto.Message
}

func wrapCheckerReq(req proto.Message) proto.Message {
	if common.InterfaceIsNil(req) {
		return nil
	}
	return &checkerRequest{req}
}

func (svc *mgmtSvc) unwrapCheckerReq(req proto.Message) (proto.Message, error) {
	cr, ok := req.(*checkerRequest)
	if ok {
		return cr.Message, nil
	}

	if svc.checkerIsEnabled() {
		return nil, checker.FaultCheckerEnabled
	}

	return req, nil
}

func (svc *mgmtSvc) makeCheckerCall(ctx context.Context, method drpc.Method, req proto.Message) (*drpc.Response, error) {
	if err := svc.checkLeaderRequest(wrapCheckerReq(req)); err != nil {
		return nil, err
	}
	if !svc.checkerIsEnabled() {
		return nil, checker.FaultCheckerNotEnabled
	}

	if err := svc.checkMemberStates(
		system.MemberStateAdminExcluded,
		system.MemberStateCheckerStarted,
	); err != nil {
		return nil, err
	}

	return svc.harness.CallDrpc(ctx, method, req)
}

type poolCheckerReq interface {
	proto.Message
	GetUuids() []string
}

func (svc *mgmtSvc) makePoolCheckerCall(ctx context.Context, method drpc.Method, req poolCheckerReq) (*drpc.Response, error) {
	poolUuids := make([]string, len(req.GetUuids()))
	for i, id := range req.GetUuids() {
		uuid, err := svc.resolvePoolID(id)
		if err != nil {
			return nil, err
		}
		poolUuids[i] = uuid.String()
	}

	switch r := req.(type) {
	case *mgmtpb.CheckStartReq:
		checkRanks, err := svc.sysdb.MemberRanks(system.MemberStateCheckerStarted)
		if err != nil {
			return nil, err
		}

		r.Ranks = ranklist.RanksToUint32(checkRanks)
		r.Uuids = poolUuids
	case *mgmtpb.CheckStopReq:
		r.Uuids = poolUuids
	case *mgmtpb.CheckQueryReq:
		r.Uuids = poolUuids
	default:
		return nil, errors.Errorf("unexpected request type %T", req)
	}

	return svc.makeCheckerCall(ctx, method, req)
}

func (svc *mgmtSvc) startSystemRanks(ctx context.Context, sys string) error {
	// Use the group membership to determine the set of ranks
	// that are available to be started.
	gm, err := svc.sysdb.GroupMap()
	if err != nil {
		return errors.Wrap(err, "failed to get group map")
	}
	availRanks := ranklist.NewRankSet()
	for rank := range gm.RankEntries {
		availRanks.Add(rank)
	}

	// Finally, restart all of the ranks so that they join in
	// checker mode.
	startReq := &mgmtpb.SystemStartReq{
		Sys:       sys,
		CheckMode: svc.checkerIsEnabled(),
		Ranks:     availRanks.String(),
	}
	if _, err := svc.SystemStart(ctx, startReq); err != nil {
		return errors.Wrap(err, "failed to start all ranks")
	}

	return nil
}

func (svc *mgmtSvc) SystemCheckEnable(ctx context.Context, req *mgmtpb.CheckEnableReq) (*mgmtpb.DaosResp, error) {
	if err := svc.checkLeaderRequest(wrapCheckerReq(req)); err != nil {
		return nil, err
	}

	if svc.checkerIsEnabled() {
		return &mgmtpb.DaosResp{Status: int32(daos.Already)}, nil
	}

	if err := svc.checkMemberStates(
		system.MemberStateAdminExcluded,
		system.MemberStateStopped,
	); err != nil {
		return nil, err
	}

	if err := svc.enableChecker(); err != nil {
		return nil, err
	}

	if err := svc.startSystemRanks(ctx, req.Sys); err != nil {
		return nil, err
	}

	return &mgmtpb.DaosResp{}, nil
}

func (svc *mgmtSvc) SystemCheckDisable(ctx context.Context, req *mgmtpb.CheckDisableReq) (*mgmtpb.DaosResp, error) {
	if err := svc.checkLeaderRequest(wrapCheckerReq(req)); err != nil {
		return nil, err
	}

	if !svc.checkerIsEnabled() {
		return &mgmtpb.DaosResp{Status: int32(daos.Already)}, nil
	}

	if err := svc.disableChecker(); err != nil {
		return nil, err
	}

	// Stop all of the ranks that are currently running in checker mode.
	checkRanks, err := svc.sysdb.MemberRanks(system.MemberStateCheckerStarted)
	if err != nil {
		return nil, err
	}
	stopReq := &mgmtpb.SystemStopReq{
		Sys:   req.Sys,
		Force: true,
		Ranks: ranklist.RankSetFromRanks(checkRanks).String(),
	}
	if _, err := svc.SystemStop(ctx, stopReq); err != nil {
		return nil, errors.Wrap(err, "failed to stop all checker ranks")
	}

	return &mgmtpb.DaosResp{}, nil
}

// SystemCheckStart starts a system check. The checker must be explicitly enabled to successfully
// start a check.
func (svc *mgmtSvc) SystemCheckStart(ctx context.Context, req *mgmtpb.CheckStartReq) (*mgmtpb.CheckStartResp, error) {
	if err := svc.checkLeaderRequest(wrapCheckerReq(req)); err != nil {
		return nil, err
	}

	pm, err := svc.getCheckerPolicyMap()
	if err != nil {
		return nil, err
	}

	// Allow the request to override any policies stored in the policy map.
	for _, pol := range req.Policies {
		pm[pol.InconsistCas] = pol
	}
	req.Policies = make([]*mgmtpb.CheckInconsistPolicy, 0, len(pm))
	for _, pol := range pm {
		req.Policies = append(req.Policies, pol)
	}

	dResp, err := svc.makePoolCheckerCall(ctx, drpc.MethodCheckerStart, req)
	if err != nil {
		return nil, err
	}

	resp := new(mgmtpb.CheckStartResp)
	if err := proto.Unmarshal(dResp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal CheckStart response")
	}

	if resp.Status > 0 {
		if len(req.Uuids) == 0 {
			svc.log.Debug("resetting checker findings DB")
			if err := svc.sysdb.ResetCheckerData(); err != nil {
				return nil, errors.Wrap(err, "failed to reset checker finding database")
			}
		} else {
			pools := strings.Join(req.Uuids, ", ")
			svc.log.Debugf("removing old checker findings for pools: %s", pools)
			if err := svc.sysdb.RemoveCheckerFindingsForPools(req.Uuids...); err != nil {
				return nil, errors.Wrapf(err, "failed to remove old findings for pools: %s", pools)
			}
		}
		resp.Status = 0 // reset status to indicate success
	}

	return resp, nil
}

func (svc *mgmtSvc) SystemCheckStop(ctx context.Context, req *mgmtpb.CheckStopReq) (*mgmtpb.CheckStopResp, error) {
	if err := svc.checkLeaderRequest(wrapCheckerReq(req)); err != nil {
		return nil, err
	}

	dResp, err := svc.makePoolCheckerCall(ctx, drpc.MethodCheckerStop, req)
	if err != nil {
		return nil, err
	}

	resp := new(mgmtpb.CheckStopResp)
	if err := proto.Unmarshal(dResp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal CheckStop response")
	}

	return resp, nil
}

func (svc *mgmtSvc) SystemCheckQuery(ctx context.Context, req *mgmtpb.CheckQueryReq) (*mgmtpb.CheckQueryResp, error) {
	if err := svc.checkLeaderRequest(wrapCheckerReq(req)); err != nil {
		return nil, err
	}

	resp := new(mgmtpb.CheckQueryResp)
	if len(req.GetSeqs()) > 0 {
		req.Shallow = true
	}

	if !req.Shallow {
		dResp, err := svc.makePoolCheckerCall(ctx, drpc.MethodCheckerQuery, req)
		if err != nil {
			return nil, err
		}

		if err = proto.Unmarshal(dResp.Body, resp); err != nil {
			return nil, errors.Wrap(err, "unmarshal CheckQuery response")
		}
	}

	cfList, err := svc.sysdb.GetCheckerFindings(req.GetSeqs()...)
	if err != nil {
		return nil, err
	}

	for _, f := range cfList {
		resp.Reports = append(resp.Reports, &f.CheckReport)
	}

	return resp, nil
}

type policyMap map[chkpb.CheckInconsistClass]*mgmtpb.CheckInconsistPolicy

func (svc *mgmtSvc) getCheckerPolicyMap() (policyMap, error) {
	pm := make(policyMap)
	polStr, err := system.GetMgmtProperty(svc.sysdb, checkerPoliciesKey)
	if err != nil {
		if !system.IsErrSystemAttrNotFound(err) {
			return nil, errors.Wrap(err, "failed to get checker policies map")
		}
		return pm, nil
	}

	var polList []*mgmtpb.CheckInconsistPolicy
	if err := json.Unmarshal([]byte(polStr), &polList); err != nil {
		return nil, errors.Wrap(err, "failed to unmarshal checker policies map")
	}

	for _, pol := range polList {
		pm[pol.InconsistCas] = pol
	}

	return pm, nil
}

func (svc *mgmtSvc) SystemCheckGetPolicy(ctx context.Context, req *mgmtpb.CheckGetPolicyReq) (*mgmtpb.CheckGetPolicyResp, error) {
	if err := svc.checkLeaderRequest(wrapCheckerReq(req)); err != nil {
		return nil, err
	}

	dReq := &mgmtpb.CheckPropReq{Sys: req.Sys}
	dResp, err := svc.makeCheckerCall(ctx, drpc.MethodCheckerProp, dReq)
	if err != nil {
		return nil, err
	}

	getPropResp := new(mgmtpb.CheckPropResp)
	if err = proto.Unmarshal(dResp.Body, getPropResp); err != nil {
		return nil, errors.Wrap(err, "unmarshal CheckProp response")
	}

	reqClasses := make(map[chkpb.CheckInconsistClass]struct{})
	for _, c := range req.Classes {
		reqClasses[c] = struct{}{}
	}

	pm, err := svc.getCheckerPolicyMap()
	if err != nil {
		return nil, err
	}

	for i, enginePol := range getPropResp.Policies {
		if savedPol, found := pm[enginePol.InconsistCas]; found {
			getPropResp.Policies[i] = savedPol
		}
	}

	resp := &mgmtpb.CheckGetPolicyResp{
		Status: getPropResp.Status,
		Flags:  getPropResp.Flags,
	}
	for _, p := range getPropResp.Policies {
		if p.InconsistCas == chkpb.CheckInconsistClass_CIC_NONE {
			continue
		}

		// If the request specified a list of classes, only return policies
		// for those classes.
		if len(reqClasses) > 0 {
			if _, ok := reqClasses[p.InconsistCas]; ok {
				resp.Policies = append(resp.Policies, p)
			}
			continue
		}

		// The engine side sends back a full policy array, so filter out array
		// elements with policies for undefined classes.
		if _, ok := chkpb.CheckInconsistClass_name[int32(p.InconsistCas)]; ok {
			resp.Policies = append(resp.Policies, p)
		}
	}

	return resp, nil
}

func (svc *mgmtSvc) setCheckerPolicyMap(polList []*mgmtpb.CheckInconsistPolicy) error {
	polStr, err := json.Marshal(polList)
	if err != nil {
		return errors.Wrap(err, "failed to marshal checker policies map")
	}

	return system.SetMgmtProperty(svc.sysdb, checkerPoliciesKey, string(polStr))
}

func (svc *mgmtSvc) SystemCheckSetPolicy(ctx context.Context, req *mgmtpb.CheckSetPolicyReq) (*mgmtpb.DaosResp, error) {
	if err := svc.checkLeaderRequest(wrapCheckerReq(req)); err != nil {
		return nil, err
	}

	if err := svc.setCheckerPolicyMap(req.Policies); err != nil {
		return nil, err
	}

	return &mgmtpb.DaosResp{}, nil
}

func (svc *mgmtSvc) SystemCheckRepair(ctx context.Context, req *mgmtpb.CheckActReq) (*mgmtpb.CheckActResp, error) {
	if err := svc.checkLeaderRequest(wrapCheckerReq(req)); err != nil {
		return nil, err
	}

	f, err := svc.sysdb.GetCheckerFinding(req.Seq)
	if err != nil {
		return nil, err
	}

	if !f.HasChoice(req.Act) {
		return nil, errors.Errorf("invalid action %s (must be one of %s)", req.Act, f.ValidChoicesString())
	}

	dResp, err := svc.makeCheckerCall(ctx, drpc.MethodCheckerAction, req)
	if err != nil {
		return nil, err
	}

	resp := new(mgmtpb.CheckActResp)
	if err = proto.Unmarshal(dResp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal CheckRepair response")
	}

	if resp.Status == 0 {
		if err := svc.sysdb.SetCheckerFindingAction(req.Seq, int32(req.Act)); err != nil {
			return nil, err
		}
		svc.log.Debugf("Set action %s for finding %d", req.Act, req.Seq)
	}

	return resp, nil
}
