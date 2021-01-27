//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"syscall"
	"time"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	instanceUpdateDelay = 500 * time.Millisecond
)

// pollInstanceState waits for either context to be cancelled/timeout or for the
// provided validate function to return true for each of the provided instances.
//
// Returns true if all instances return true from the validate function within
// the given timeout, false otherwise. Error is returned if parent context is
// cancelled or times out.
func pollInstanceState(ctx context.Context, instances []*IOServerInstance, validate func(*IOServerInstance) bool, timeout time.Duration) (bool, error) {
	ready := make(chan struct{})
	go func() {
		for {
			select {
			case <-ctx.Done():
				return
			default:
			}

			success := true
			for _, srv := range instances {
				if !validate(srv) {
					success = false
				}
			}
			if success {
				close(ready)
				return
			}
			time.Sleep(instanceUpdateDelay)
		}
	}()

	select {
	case <-ctx.Done():
		return false, ctx.Err()
	case <-time.After(timeout):
		return false, nil
	case <-ready:
		return true, nil
	}
}

// drpcOnLocalRanks iterates over local instances issuing dRPC requests in
// parallel and returning system member results when all have been received.
func (svc *ControlService) drpcOnLocalRanks(parent context.Context, req *ctlpb.RanksReq, method drpc.Method) ([]*system.MemberResult, error) {
	ctx, cancel := context.WithTimeout(parent, svc.harness.rankReqTimeout)
	defer cancel()

	instances, err := svc.harness.FilterInstancesByRankSet(req.GetRanks())
	if err != nil {
		return nil, errors.Wrap(err, "sending request over dRPC to local ranks")
	}

	inflight := 0
	ch := make(chan *system.MemberResult)
	for _, srv := range instances {
		inflight++
		go func(s *IOServerInstance) {
			ch <- s.TryDrpc(ctx, method)
		}(srv)
	}

	results := make(system.MemberResults, 0, inflight)
	for inflight > 0 {
		result := <-ch
		inflight--
		if result == nil {
			return nil, errors.New("sending request over dRPC to local ranks: nil result")
		}
		results = append(results, result)
	}

	return results, nil
}

// PrepShutdown implements the method defined for the Management Service.
//
// Prepare data-plane instance(s) managed by control-plane for a controlled shutdown,
// identified by unique rank(s).
//
// Iterate over local instances, issuing PrepShutdown dRPCs and record results.
func (svc *ControlService) PrepShutdownRanks(ctx context.Context, req *ctlpb.RanksReq) (*ctlpb.RanksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	if len(req.GetRanks()) == 0 {
		return nil, errors.New("no ranks specified in request")
	}
	svc.log.Debugf("MgmtSvc.PrepShutdownRanks dispatch, req:%+v\n", *req)

	results, err := svc.drpcOnLocalRanks(ctx, req, drpc.MethodPrepShutdown)
	if err != nil {
		return nil, err
	}

	resp := &ctlpb.RanksResp{}
	if err := convert.Types(results, &resp.Results); err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.PrepShutdown dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// memberStateResults returns system member results reflecting whether the state
// of the given member is equivalent to the supplied desired state value.
func (svc *ControlService) memberStateResults(instances []*IOServerInstance, desiredState system.MemberState, successMsg string) (system.MemberResults, error) {
	results := make(system.MemberResults, 0, len(instances))
	for _, srv := range instances {
		rank, err := srv.GetRank()
		if err != nil {
			svc.log.Debugf("Instance %d GetRank(): %s", srv.Index(), err)
			continue
		}

		state := srv.LocalState()
		if state != desiredState {
			results = append(results, system.NewMemberResult(rank,
				errors.Errorf("want %s, got %s", desiredState, state), state))
			continue
		}

		results = append(results, &system.MemberResult{
			Rank: rank, Msg: successMsg, State: state,
		})
	}

	return results, nil
}

// StopRanks implements the method defined for the Management Service.
//
// Stop data-plane instance(s) managed by control-plane identified by unique
// rank(s). After attempting to stop instances through harness (when either all
// instances are stopped or timeout has occurred), populate response results
// based on local instance state.
func (svc *ControlService) StopRanks(ctx context.Context, req *ctlpb.RanksReq) (*ctlpb.RanksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	if len(req.GetRanks()) == 0 {
		return nil, errors.New("no ranks specified in request")
	}
	svc.log.Debugf("MgmtSvc.StopRanks dispatch, req:%+v\n", *req)

	signal := syscall.SIGINT
	if req.Force {
		signal = syscall.SIGKILL
	}

	instances, err := svc.harness.FilterInstancesByRankSet(req.GetRanks())
	if err != nil {
		return nil, err
	}

	// don't publish rank down events whilst performing controlled shutdown
	svc.events.DisableEventIDs(events.RASRankDown)
	defer svc.events.EnableEventIDs(events.RASRankDown)

	for _, srv := range instances {
		svc.log.Debugf("%d: check started", srv.Index())
		if !srv.isStarted() {
			continue
		}
		svc.log.Debugf("%d: call Stop()", srv.Index())
		if err := srv.Stop(signal); err != nil {
			return nil, errors.Wrapf(err, "sending %s", signal)
		}
	}

	// ignore poll results as we gather state immediately after
	if _, err = pollInstanceState(ctx, instances,
		func(s *IOServerInstance) bool { return !s.isStarted() },
		svc.harness.rankReqTimeout); err != nil {

		return nil, err
	}

	results, err := svc.memberStateResults(instances, system.MemberStateStopped, "system stop")
	if err != nil {
		return nil, err
	}
	resp := &ctlpb.RanksResp{}
	if err := convert.Types(results, &resp.Results); err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.StopRanks dispatch, resp:%+v\n", *resp)

	return resp, nil
}

func (svc *ControlService) queryLocalRanks(ctx context.Context, req *ctlpb.RanksReq) ([]*system.MemberResult, error) {
	if req.Force {
		return svc.drpcOnLocalRanks(ctx, req, drpc.MethodPingRank)
	}

	instances, err := svc.harness.FilterInstancesByRankSet(req.GetRanks())
	if err != nil {
		return nil, err
	}

	results := make(system.MemberResults, 0, len(instances))
	for _, srv := range instances {
		rank, err := srv.GetRank()
		if err != nil {
			// shouldn't happen, instances already filtered by ranks
			return nil, err
		}
		results = append(results, &system.MemberResult{
			Rank: rank, State: srv.LocalState(),
		})
	}

	return results, nil
}

// PingRanks implements the method defined for the Management Service.
//
// Query data-plane ranks (DAOS system members) managed by harness to verify
// responsiveness. If force flag is set in request, perform invasive ping by
// sending request over dRPC to be handled by rank process. If forced flag
// is not set in request then perform non-invasive ping by retrieving rank
// instance state (AwaitFormat/Stopped/Starting/Started) from harness.
//
// Iterate over local instances, ping and record results.
func (svc *ControlService) PingRanks(ctx context.Context, req *ctlpb.RanksReq) (*ctlpb.RanksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	if len(req.GetRanks()) == 0 {
		return nil, errors.New("no ranks specified in request")
	}

	svc.log.Debugf("MgmtSvc.PingRanks dispatch, req:%+v\n", *req)

	results, err := svc.queryLocalRanks(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := &ctlpb.RanksResp{}
	if err := convert.Types(results, &resp.Results); err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.PingRanks dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// ResetFormatRanks implements the method defined for the Management Service.
//
// Reset storage format of data-plane instances (DAOS system members) managed
// by harness.
//
// Reset formatted state of data-plane instance(s) managed by control-plane
// identified by unique rank(s). After attempting to reset instances through
// harness (when either all instances are awaiting format or timeout has
// occurred), populate response results based on local instance state.
func (svc *ControlService) ResetFormatRanks(ctx context.Context, req *ctlpb.RanksReq) (*ctlpb.RanksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	if len(req.GetRanks()) == 0 {
		return nil, errors.New("no ranks specified in request")
	}
	svc.log.Debugf("MgmtSvc.ResetFormatRanks dispatch, req:%+v\n", *req)

	instances, err := svc.harness.FilterInstancesByRankSet(req.GetRanks())
	if err != nil {
		return nil, err
	}

	savedRanks := make(map[uint32]system.Rank) // instance idx to system rank
	for _, srv := range instances {
		rank, err := srv.GetRank()
		if err != nil {
			return nil, err
		}
		savedRanks[srv.Index()] = rank

		if srv.isStarted() {
			return nil, FaultInstancesNotStopped("reset format", rank)
		}
		if err := srv.RemoveSuperblock(); err != nil {
			return nil, err
		}
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case srv.startLoop <- true: // proceed to awaiting storage format
		}
	}

	// ignore poll results as we gather state immediately after
	if _, err = pollInstanceState(ctx, instances, (*IOServerInstance).isAwaitingFormat,
		svc.harness.rankStartTimeout); err != nil {

		return nil, err
	}

	// rank cannot be pulled from superblock so use saved value
	results := make(system.MemberResults, 0, len(instances))
	for _, srv := range instances {
		var err error
		state := srv.LocalState()
		if state != system.MemberStateAwaitFormat {
			err = errors.Errorf("want %s, got %s", system.MemberStateAwaitFormat, state)
		}

		results = append(results, system.NewMemberResult(savedRanks[srv.Index()], err, state))
	}

	resp := &ctlpb.RanksResp{}
	if err := convert.Types(results, &resp.Results); err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.ResetFormatRanks dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// StartRanks implements the method defined for the Management Service.
//
// Start data-plane instance(s) managed by control-plane identified by unique
// rank(s). After attempting to start instances through harness (when either all
// instances are in ready state or timeout has occurred), populate response results
// based on local instance state.
func (svc *ControlService) StartRanks(ctx context.Context, req *ctlpb.RanksReq) (*ctlpb.RanksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	if len(req.GetRanks()) == 0 {
		return nil, errors.New("no ranks specified in request")
	}
	svc.log.Debugf("MgmtSvc.StartRanks dispatch, req:%+v\n", *req)

	instances, err := svc.harness.FilterInstancesByRankSet(req.GetRanks())
	if err != nil {
		return nil, err
	}
	for _, srv := range instances {
		if srv.isStarted() {
			continue
		}
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case srv.startLoop <- true:
		}
	}

	// ignore poll results as we gather state immediately after
	if _, err = pollInstanceState(ctx, instances, (*IOServerInstance).isReady,
		svc.harness.rankStartTimeout); err != nil {

		return nil, err
	}

	// instances will update state to "Started" through join or
	// bootstrap in membership, here just make sure instances are "Ready"
	results, err := svc.memberStateResults(instances, system.MemberStateReady, "system start")
	if err != nil {
		return nil, err
	}
	resp := &ctlpb.RanksResp{}
	if err := convert.Types(results, &resp.Results); err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.StartRanks dispatch, resp:%+v\n", *resp)

	return resp, nil
}
