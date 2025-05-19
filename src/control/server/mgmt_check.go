//
// (C) Copyright 2022-2024 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"encoding/json"
	"sort"
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
	checkerEnabledKey      = "checker_enabled"
	checkerPoliciesKey     = "checker_policies"
	checkerLatestPolicyKey = "checker_latest_policy"
)

var errNoSavedPolicies = errors.New("no previous policies have been saved")

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
		if !system.IsNotLeader(err) && !system.IsErrSystemAttrNotFound(err) &&
			!system.IsNotReplica(err) && !errors.Is(err, system.ErrUninitialized) {
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

	if err := svc.verifyCheckerReady(); err != nil {
		return nil, err
	}

	return svc.harness.CallDrpc(ctx, method, req)
}

func (svc *mgmtSvc) verifyCheckerReady() error {
	if !svc.checkerIsEnabled() {
		return checker.FaultCheckerNotEnabled
	}

	if err := svc.checkMemberStates(
		system.MemberStateAdminExcluded,
		system.MemberStateCheckerStarted,
	); err != nil {
		return err
	}

	return nil
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

// SystemCheckEnable puts the system in checker mode.
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

// SystemCheckDisable turns off checker mode for the system.
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
		Sys: req.Sys,
		// Do not force stop system, it may cause resource leak and fail next system start.
		Force: false,
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

	policies, err := svc.mergePoliciesWithCurrent(req.Policies)
	if err != nil {
		return nil, err
	}
	req.Policies = policies

	if err := svc.setLastPoliciesUsed(req.Policies); err != nil {
		svc.log.Errorf("failed to save the policies used: %s", err.Error())
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

func (svc *mgmtSvc) mergePoliciesWithCurrent(policies []*mgmtpb.CheckInconsistPolicy) ([]*mgmtpb.CheckInconsistPolicy, error) {
	pm, err := svc.getCheckerPolicyMap()
	if err != nil {
		return nil, err
	}

	// Allow the requested policies to override any policies stored in the policy map.
	for _, pol := range policies {
		pm[pol.InconsistCas] = pol
	}
	return pm.ToSlice(), nil
}

func (svc *mgmtSvc) setLastPoliciesUsed(polList []*mgmtpb.CheckInconsistPolicy) error {
	polStr, err := json.Marshal(polList)
	if err != nil {
		return errors.Wrap(err, "failed to marshal latest checker policies")
	}

	return system.SetMgmtProperty(svc.sysdb, checkerLatestPolicyKey, string(polStr))
}

// SystemCheckStop stops a running system check.
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

// SystemCheckQuery queries the state of the checker. This will indicate all known findings, as
// well as the running state.
func (svc *mgmtSvc) SystemCheckQuery(ctx context.Context, req *mgmtpb.CheckQueryReq) (*mgmtpb.CheckQueryResp, error) {
	if err := svc.checkLeaderRequest(wrapCheckerReq(req)); err != nil {
		return nil, err
	}

	resp := new(mgmtpb.CheckQueryResp)
	if len(req.GetSeqs()) > 0 {
		req.Shallow = true
	}

	uuids := common.NewStringSet(req.Uuids...)
	wantUUID := func(uuid string) bool {
		return len(uuids) == 0 || uuids.Has(uuid)
	}

	reports := []*chkpb.CheckReport{}

	if !req.Shallow {
		dResp, err := svc.makePoolCheckerCall(ctx, drpc.MethodCheckerQuery, req)
		if err != nil {
			return nil, err
		}

		if err = proto.Unmarshal(dResp.Body, resp); err != nil {
			return nil, errors.Wrap(err, "unmarshal CheckQuery response")
		}

		for _, r := range resp.Reports {
			if wantUUID(r.PoolUuid) {
				reports = append(reports, r)
			}
		}
	}

	// Collect saved older reports
	cfList, err := svc.sysdb.GetCheckerFindings(req.GetSeqs()...)
	if err != nil {
		return nil, err
	}

	for _, f := range cfList {
		if wantUUID(f.PoolUuid) {
			reports = append(reports, &f.CheckReport)
		}
	}
	sort.Slice(reports, func(i, j int) bool {
		return reports[i].Seq < reports[j].Seq
	})
	resp.Reports = reports

	return resp, nil
}

type policyMap map[chkpb.CheckInconsistClass]*mgmtpb.CheckInconsistPolicy

// ToSlice returns a sorted slice of policies from the map.
func (pm policyMap) ToSlice(classes ...chkpb.CheckInconsistClass) []*mgmtpb.CheckInconsistPolicy {
	policies := []*mgmtpb.CheckInconsistPolicy{}

	if len(classes) > 0 {
		for _, cls := range classes {
			if pol, found := pm[cls]; found {
				policies = append(policies, pol)
			}
		}
	} else {
		for _, pol := range pm {
			policies = append(policies, pol)
		}
	}

	sort.Slice(policies, func(i, j int) bool {
		return policies[i].InconsistCas < policies[j].InconsistCas
	})

	return policies
}

func (svc *mgmtSvc) getCheckerPolicyMap() (policyMap, error) {
	if pm, err := svc.getCheckerPolicyMapWithKey(checkerPoliciesKey); err == nil {
		return pm, nil
	} else if !system.IsErrSystemAttrNotFound(err) {
		return nil, errors.Wrap(err, "failed to get checker policies map")
	}

	// No policies have been set
	pm := svc.defaultPolicyMap()

	if err := svc.setCheckerPolicyMap(pm.ToSlice()); err != nil {
		svc.log.Errorf("failed to set default policies: %s", err.Error())
	}
	return pm, nil
}

func (svc *mgmtSvc) getCheckerPolicyMapWithKey(key string) (policyMap, error) {
	var polList []*mgmtpb.CheckInconsistPolicy
	polStr, err := system.GetMgmtProperty(svc.sysdb, key)
	if err != nil {
		return nil, err
	}

	if err := json.Unmarshal([]byte(polStr), &polList); err != nil {
		return nil, errors.Wrap(err, "failed to unmarshal checker policies map")
	}

	pm := make(policyMap)

	for _, pol := range polList {
		pm[pol.InconsistCas] = pol
	}

	return pm, nil
}

func (svc *mgmtSvc) defaultPolicyMap() policyMap {
	pm := make(policyMap)
	for cicEnum := range chkpb.CheckInconsistClass_name {
		cic := chkpb.CheckInconsistClass(cicEnum)
		if cic == chkpb.CheckInconsistClass_CIC_NONE || cic == chkpb.CheckInconsistClass_CIC_UNKNOWN {
			continue
		}
		pm[cic] = &mgmtpb.CheckInconsistPolicy{
			InconsistCas: cic,
			InconsistAct: chkpb.CheckInconsistAction_CIA_DEFAULT,
		}
	}
	return pm
}

func (svc *mgmtSvc) getLastPoliciesUsed() (policyMap, error) {
	pm, err := svc.getCheckerPolicyMapWithKey(checkerLatestPolicyKey)
	if system.IsErrSystemAttrNotFound(err) {
		return nil, errNoSavedPolicies
	}
	return pm, nil
}

// SystemCheckGetPolicy fetches the policies for the system checker.
func (svc *mgmtSvc) SystemCheckGetPolicy(ctx context.Context, req *mgmtpb.CheckGetPolicyReq) (*mgmtpb.CheckGetPolicyResp, error) {
	if err := svc.checkLeaderRequest(wrapCheckerReq(req)); err != nil {
		return nil, err
	}

	if err := svc.verifyCheckerReady(); err != nil {
		return nil, err
	}

	resp := new(mgmtpb.CheckGetPolicyResp)

	var pm policyMap
	var err error
	if req.LastUsed {
		pm, err = svc.getLastPoliciesUsed()
		if errors.Is(err, errNoSavedPolicies) {
			pm, err = svc.getCheckerPolicyMap()
		}
	} else {
		pm, err = svc.getCheckerPolicyMap()
	}
	if err != nil {
		return nil, err
	}

	resp.Policies = pm.ToSlice(req.Classes...)

	return resp, nil
}

func (svc *mgmtSvc) setCheckerPolicyMap(polList []*mgmtpb.CheckInconsistPolicy) error {
	polStr, err := json.Marshal(polList)
	if err != nil {
		return errors.Wrap(err, "failed to marshal checker policies map")
	}

	return system.SetMgmtProperty(svc.sysdb, checkerPoliciesKey, string(polStr))
}

// SystemCheckSetPolicy sets checker policies in the policy map.
func (svc *mgmtSvc) SystemCheckSetPolicy(ctx context.Context, req *mgmtpb.CheckSetPolicyReq) (*mgmtpb.DaosResp, error) {
	if err := svc.checkLeaderRequest(wrapCheckerReq(req)); err != nil {
		return nil, err
	}

	if err := svc.verifyCheckerReady(); err != nil {
		return nil, err
	}

	policies, err := svc.mergePoliciesWithCurrent(req.Policies)
	if err != nil {
		return nil, err
	}

	if err := svc.setCheckerPolicyMap(policies); err != nil {
		return nil, err
	}

	return &mgmtpb.DaosResp{}, nil
}

// SystemCheckRepair repairs a previous checker finding.
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
