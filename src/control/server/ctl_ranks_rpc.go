//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"syscall"
	"time"

	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	// instanceUpdateDelay is the polling time period
	instanceUpdateDelay = 500 * time.Millisecond
)

// pollInstanceState waits for either context to be cancelled/timeout or for the
// provided validate function to return true for each of the provided instances.
//
// Returns true if all instances return true from the validate function, false
// if context is cancelled before.
func pollInstanceState(ctx context.Context, instances []Engine, validate func(Engine) bool) error {
	ready := make(chan struct{})
	go func() {
		for {
			select {
			case <-ctx.Done():
				return
			default:
			}

			success := true
			for _, ei := range instances {
				if !validate(ei) {
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
		return ctx.Err()
	case <-ready:
		return nil
	}
}

// PrepShutdownRanks implements the method defined for the Management Service.
//
// Prepare data-plane instance(s) managed by control-plane for a controlled shutdown,
// identified by unique rank(s).
//
// Iterate over local instances, issue PrepShutdown dRPCs and record results.
func (svc *ControlService) PrepShutdownRanks(ctx context.Context, req *ctlpb.RanksReq) (*ctlpb.RanksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	if len(req.GetRanks()) == 0 {
		return nil, errors.New("no ranks specified in request")
	}

	// iterate over local instances issuing dRPC requests in parallel and return system member
	// results when all have been received.
	instances, err := svc.harness.FilterInstancesByRankSet(req.GetRanks())
	if err != nil {
		return nil, errors.Wrap(err, "filtering instances by rank set")
	}

	ch := make(chan *system.MemberResult, len(instances))
	for _, ei := range instances {
		if !ei.IsReady() {
			rank, err := ei.GetRank()
			svc.log.Debugf("skip prep-shutdown as rank %d is dead", rank)
			// If rank is already dead, return successful result.
			ch <- system.NewMemberResult(rank, err, system.MemberStateStopped)
			continue
		}

		go func(ctx context.Context, e Engine) {
			select {
			case <-ctx.Done():
				ch <- nil
			case ch <- e.tryDrpc(ctx, drpc.MethodPrepShutdown):
			}
		}(ctx, ei)
	}

	results := make(system.MemberResults, 0, len(instances))
	for len(results) < len(instances) {
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case result := <-ch:
			if result == nil {
				return nil, errors.New("tryDrpc returned nil result")
			}
			results = append(results, result)
		}
	}

	resp := &ctlpb.RanksResp{}
	if err := convert.Types(results, &resp.Results); err != nil {
		return nil, err
	}

	return resp, nil
}

// memberStateResults returns system member results reflecting whether the state
// of the given member is equivalent to the supplied desired state value.
func (svc *ControlService) memberStateResults(instances []Engine, tgtState system.MemberState, okMsg, failMsg string) (system.MemberResults, error) {
	results := make(system.MemberResults, 0, len(instances))
	for _, ei := range instances {
		rank, err := ei.GetRank()
		if err != nil {
			svc.log.Debugf("skip MemberResult, Instance %d GetRank(): %s", ei.Index(), err)
			continue
		}

		state := ei.LocalState()
		if state != tgtState {
			results = append(results, system.NewMemberResult(rank, errors.Errorf(failMsg),
				system.MemberStateErrored))
			continue
		}

		res := system.NewMemberResult(rank, nil, state)
		res.Msg = okMsg
		results = append(results, res)
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

	signal := syscall.SIGINT
	if req.Force {
		signal = syscall.SIGKILL
	}

	instances, err := svc.harness.FilterInstancesByRankSet(req.GetRanks())
	if err != nil {
		return nil, err
	}

	// don't publish rank down events whilst performing controlled shutdown
	svc.events.DisableEventIDs(events.RASEngineDied)
	defer svc.events.EnableEventIDs(events.RASEngineDied)

	for _, ei := range instances {
		if !ei.IsStarted() {
			continue
		}
		if err := ei.Stop(signal); err != nil {
			return nil, errors.Wrapf(err, "sending %s", signal)
		}
	}

	// ignore poll results as we gather state immediately after
	pollFn := func(e Engine) bool { return !e.IsStarted() }
	if err := pollInstanceState(ctx, instances, pollFn); err != nil {
		return nil, errors.Wrap(err, "waiting for engines to stop")
	}

	results, err := svc.memberStateResults(instances, system.MemberStateStopped, "system stop",
		"system stop: rank failed to stop")
	if err != nil {
		return nil, err
	}
	resp := &ctlpb.RanksResp{}
	if err := convert.Types(results, &resp.Results); err != nil {
		return nil, err
	}

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

	instances, err := svc.harness.FilterInstancesByRankSet(req.GetRanks())
	if err != nil {
		return nil, err
	}

	savedRanks := make(map[uint32]ranklist.Rank) // instance idx to system rank
	for _, ei := range instances {
		rank, err := ei.GetRank()
		if err != nil {
			return nil, err
		}
		savedRanks[ei.Index()] = rank

		if ei.IsStarted() {
			return nil, FaultInstancesNotStopped("reset format", rank)
		}
		if err := ei.RemoveSuperblock(); err != nil {
			return nil, err
		}
		ei.requestStart(ctx)
	}

	// ignore poll results as we gather state immediately after
	pollFn := func(e Engine) bool { return e.isAwaitingFormat() }
	if err := pollInstanceState(ctx, instances, pollFn); err != nil {
		return nil, errors.Wrap(err, "waiting for engines to await format")
	}

	// rank cannot be pulled from superblock so use saved value
	results := make(system.MemberResults, 0, len(instances))
	for _, ei := range instances {
		var err error
		state := ei.LocalState()
		if state != system.MemberStateAwaitFormat {
			err = errors.Errorf("want %s, got %s", system.MemberStateAwaitFormat, state)
		}

		results = append(results, system.NewMemberResult(savedRanks[ei.Index()], err, state))
	}

	resp := &ctlpb.RanksResp{}
	if err := convert.Types(results, &resp.Results); err != nil {
		return nil, err
	}

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

	instances, err := svc.harness.FilterInstancesByRankSet(req.GetRanks())
	if err != nil {
		return nil, err
	}
	for _, ei := range instances {
		if ei.IsStarted() {
			continue
		}
		ei.requestStart(ctx)
	}

	// ignore poll results as we gather state immediately after
	pollFn := func(e Engine) bool { return e.IsReady() }
	if err := pollInstanceState(ctx, instances, pollFn); err != nil {
		return nil, errors.Wrap(err, "waiting for engines to start")
	}

	// instances will update state to "Started" through join or
	// bootstrap in membership, here just make sure instances are "Ready"
	results, err := svc.memberStateResults(instances, system.MemberStateReady, "system start",
		"system start: rank failed to start")
	if err != nil {
		return nil, err
	}
	resp := &ctlpb.RanksResp{}
	if err := convert.Types(results, &resp.Results); err != nil {
		return nil, err
	}

	return resp, nil
}

// If reset flags are set, pull values from config before merging DD_SUBSYS into D_LOG_MASK and
// setting the result in the request.
func updateSetLogMasksReq(cfg *engine.Config, req *ctlpb.SetLogMasksReq) error {
	msg := "request"
	if req.ResetMasks {
		msg = "config"
		req.Masks = cfg.LogMask
	}
	if req.Masks == "" {
		return errors.Errorf("empty log masks in %s", msg)
	}

	if req.ResetStreams {
		streams, err := cfg.ReadLogDbgStreams()
		if err != nil {
			return err
		}
		req.Streams = streams
	}

	if req.ResetSubsystems {
		subsystems, err := cfg.ReadLogSubsystems()
		if err != nil {
			return err
		}
		req.Subsystems = subsystems
	}

	newMasks, err := engine.MergeLogEnvVars(req.Masks, req.Subsystems)
	if err != nil {
		return err
	}
	req.Masks = newMasks
	req.Subsystems = ""

	return nil
}

// SetEngineLogMasks calls into each engine over dRPC to set loglevel at runtime.
func (svc *ControlService) SetEngineLogMasks(ctx context.Context, req *ctlpb.SetLogMasksReq) (*ctlpb.SetLogMasksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}

	resp := new(ctlpb.SetLogMasksResp)
	instances := svc.harness.Instances()
	resp.Errors = make([]string, len(instances))

	for idx, ei := range instances {
		eReq := *req // local per-engine copy

		if int(ei.Index()) != idx {
			svc.log.Errorf("engine instance index %d doesn't match engine.Index %d",
				idx, ei.Index())
		}

		if err := updateSetLogMasksReq(svc.srvCfg.Engines[idx], &eReq); err != nil {
			resp.Errors[idx] = err.Error()
			continue
		}
		svc.log.Debugf("setting engine %d log masks %q, streams %q and subsystems %q",
			ei.Index(), eReq.Masks, eReq.Streams, eReq.Subsystems)

		dresp, err := ei.CallDrpc(ctx, drpc.MethodSetLogMasks, &eReq)
		if err != nil {
			resp.Errors[idx] = err.Error()
			continue
		}

		engineResp := new(ctlpb.SetLogMasksResp)
		if err = proto.Unmarshal(dresp.Body, engineResp); err != nil {
			return nil, err
		}

		if engineResp.Status != 0 {
			resp.Errors[idx] = daos.Status(engineResp.Status).Error()
		}
	}

	return resp, nil
}
