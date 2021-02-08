//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"math/rand"
	"sort"
	"strings"
	"time"

	"github.com/golang/protobuf/proto"
	"github.com/google/uuid"
	"github.com/pkg/errors"
	"golang.org/x/net/context"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	// DefaultPoolScmRatio defines the default SCM:NVMe ratio for
	// requests that do not specify one.
	DefaultPoolScmRatio = 0.06
	// DefaultPoolServiceReps defines a default value for pool create
	// requests that do not specify a value. If there are fewer than this
	// number of ranks available, then the default falls back to 1.
	DefaultPoolServiceReps = 3
	// MaxPoolServiceReps defines the maximum number of pool service
	// replicas that may be configured when creating a pool.
	MaxPoolServiceReps = 13
)

type poolServiceReq interface {
	proto.Message
	GetUuid() string
	GetSvcRanks() []uint32
	SetSvcRanks(rl []uint32)
}

func (svc *mgmtSvc) makePoolServiceCall(ctx context.Context, method drpc.Method, req poolServiceReq) (*drpc.Response, error) {
	if len(req.GetSvcRanks()) == 0 {
		rl, err := svc.getPoolServiceRanks(req.GetUuid())
		if err != nil {
			return nil, err
		}
		req.SetSvcRanks(rl)
	}

	return svc.harness.CallDrpc(ctx, method, req)
}

func (svc *mgmtSvc) getPoolServiceRanks(uuidStr string) ([]uint32, error) {
	uuid, err := uuid.Parse(uuidStr)
	if err != nil {
		return nil, errors.Wrapf(err, "failed to parse request uuid %q", uuidStr)
	}

	ps, err := svc.sysdb.FindPoolServiceByUUID(uuid)
	if err != nil {
		return nil, err
	}

	if ps.State != system.PoolServiceStateReady {
		return nil, drpc.DaosTryAgain
	}

	readyRanks := make([]system.Rank, 0, len(ps.Replicas))
	for _, r := range ps.Replicas {
		m, err := svc.sysdb.FindMemberByRank(r)
		if err != nil {
			return nil, err
		}
		if m.State()&system.AvailableMemberFilter == 0 {
			continue
		}
		readyRanks = append(readyRanks, r)
	}

	if len(readyRanks) == 0 {
		return nil, errors.Errorf("unable to find any available service ranks for pool %s", uuid)
	}

	return system.RanksToUint32(readyRanks), nil
}

// calculateCreateStorage determines the amount of SCM/NVMe storage to
// allocate per server in order to fulfill the create request, if those
// values are not already supplied as part of the request.
func (svc *mgmtSvc) calculateCreateStorage(req *mgmtpb.PoolCreateReq) error {
	instances := svc.harness.Instances()
	if len(instances) < 1 {
		return errors.New("harness has no managed instances")
	}

	if len(req.GetRanks()) == 0 {
		return errors.New("zero ranks in calculateCreateStorage()")
	}
	if req.GetScmratio() == 0 {
		req.Scmratio = DefaultPoolScmRatio
	}

	storagePerRank := func(total uint64) uint64 {
		return total / uint64(len(req.GetRanks()))
	}

	switch {
	case len(instances[0].bdevConfig().DeviceList) == 0:
		svc.log.Info("config has 0 bdevs; excluding NVMe from pool create request")
		if req.GetScmbytes() == 0 {
			req.Scmbytes = storagePerRank(req.GetTotalbytes())
		}
		req.Nvmebytes = 0
	case req.GetTotalbytes() > 0:
		req.Nvmebytes = storagePerRank(req.GetTotalbytes())
		req.Scmbytes = storagePerRank(uint64(float64(req.GetTotalbytes()) * req.GetScmratio()))
	}

	// zero these out as they're not needed anymore
	req.Totalbytes = 0
	req.Scmratio = 0

	targetCount := instances[0].GetTargetCount()
	if targetCount == 0 {
		return errors.New("zero target count")
	}
	minNvmeRequired := ioserver.NvmeMinBytesPerTarget * uint64(targetCount)

	if req.Nvmebytes != 0 && req.Nvmebytes < minNvmeRequired {
		return FaultPoolNvmeTooSmall(req.Nvmebytes, targetCount)
	}
	if req.Scmbytes < ioserver.ScmMinBytesPerTarget*uint64(targetCount) {
		return FaultPoolScmTooSmall(req.Scmbytes, targetCount)
	}

	return nil
}

// PoolCreate implements the method defined for the Management Service.
//
// Validate minimum SCM/NVMe pool size per VOS target, pool size request params
// are per-ioserver so need to be larger than (minimum_target_allocation *
// target_count).
func (svc *mgmtSvc) PoolCreate(ctx context.Context, req *mgmtpb.PoolCreateReq) (resp *mgmtpb.PoolCreateResp, err error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("MgmtSvc.PoolCreate dispatch, req:%+v\n", req)

	uuid, err := uuid.Parse(req.GetUuid())
	if err != nil {
		return nil, errors.Wrapf(err, "failed to parse pool UUID %q", req.GetUuid())
	}

	resp = new(mgmtpb.PoolCreateResp)
	ps, err := svc.sysdb.FindPoolServiceByUUID(uuid)
	if ps != nil {
		svc.log.Debugf("found pool %s state=%s", ps.PoolUUID, ps.State)
		resp.Status = int32(drpc.DaosAlready)
		if ps.State == system.PoolServiceStateCreating {
			resp.Status = int32(drpc.DaosTryAgain)
		}
		return resp, nil
	}
	if _, ok := err.(*system.ErrPoolNotFound); !ok {
		return nil, err
	}

	if _, err := svc.sysdb.FindPoolServiceByLabel(req.GetName()); err == nil {
		return nil, FaultPoolDuplicateLabel(req.GetName())
	}

	allRanks, err := svc.sysdb.MemberRanks(system.AvailableMemberFilter)
	if err != nil {
		return nil, err
	}

	if len(req.GetRanks()) > 0 {
		// If the request supplies a specific rank list, use it. Note that
		// the rank list may include downed ranks, in which case the create
		// will fail with an error.
		reqRanks := system.RanksFromUint32(req.GetRanks())
		// Create a RankSet to sort/dedupe the ranks.
		reqRanks = system.RankSetFromRanks(reqRanks).Ranks()
		if err != nil {
			return nil, err
		}

		if invalid := system.CheckRankMembership(allRanks, reqRanks); len(invalid) > 0 {
			return nil, FaultPoolInvalidRanks(invalid)
		}

		req.Ranks = system.RanksToUint32(reqRanks)
	} else {
		// Otherwise, create the pool across the requested number of
		// available ranks in the system (if the request does not
		// specify a number of ranks, all are used).
		nRanks := len(allRanks)
		if req.GetNumranks() > 0 {
			nRanks = int(req.GetNumranks())

			// TODO (DAOS-6263): Improve rank selection algorithm.
			// In the short term, we can just randomize the set of
			// available ranks in order to avoid always choosing the
			// first N ranks.
			rand.Seed(time.Now().UnixNano())
			rand.Shuffle(len(allRanks), func(i, j int) {
				allRanks[i], allRanks[j] = allRanks[j], allRanks[i]
			})
		}

		req.Ranks = make([]uint32, nRanks)
		for i := 0; i < nRanks; i++ {
			req.Ranks[i] = allRanks[i].Uint32()
		}
		sort.Slice(req.Ranks, func(i, j int) bool { return req.Ranks[i] < req.Ranks[j] })
	}

	if len(req.GetRanks()) == 0 {
		return nil, errors.New("pool request contains zero target ranks")
	}

	// Set the number of service replicas to a reasonable default
	// if the request didn't specify. Note that the number chosen
	// should not be even in order to work best with the raft protocol's
	// 2N+1 resiliency model.
	if req.GetNumsvcreps() == 0 {
		req.Numsvcreps = DefaultPoolServiceReps
		if len(req.GetRanks()) < DefaultPoolServiceReps {
			req.Numsvcreps = 1
		}
	} else if req.GetNumsvcreps() > MaxPoolServiceReps {
		return nil, FaultPoolInvalidServiceReps
	}

	// IO server needs the fault domain tree for placement purposes
	req.FaultDomains = svc.sysdb.FaultDomainTree().ToProto()

	if err := svc.calculateCreateStorage(req); err != nil {
		return nil, err
	}

	ps = system.NewPoolService(uuid, req.GetScmbytes(), req.GetNvmebytes(), system.RanksFromUint32(req.GetRanks()))
	ps.PoolLabel = req.GetName()
	if err := svc.sysdb.AddPoolService(ps); err != nil {
		return nil, err
	}

	defer func() {
		var cuErr error
		switch {
		// No pool service created; nothing to clean up
		case ps == nil:
			return
		// No error and pool create went OK, nothing to do
		case err == nil && resp.GetStatus() == 0:
			return
		// Error after pool was created
		case err != nil && resp.GetStatus() == 0:
			svc.log.Errorf("cleaning up pool %s due to create failure: %q", req.Uuid, err)

			var pdResp *mgmtpb.PoolDestroyResp
			pdResp, cuErr = svc.PoolDestroy(ctx,
				&mgmtpb.PoolDestroyReq{
					Uuid:     req.Uuid,
					Sys:      req.Sys,
					Force:    true,
					SvcRanks: req.Ranks,
				})
			if cuErr != nil {
				svc.log.Errorf("error while destroying pool %s: %s", req.Uuid, cuErr)
				break
			}
			if pdResp.GetStatus() != 0 {
				cuErr = errors.Errorf("failed to destroy pool %s: %s",
					req.Uuid, drpc.DaosStatus(pdResp.GetStatus()))
			}
		}

		if cuErr == nil {
			svc.log.Errorf("removed pool service entry for %s in cleanup", req.Uuid)
			return
		}
	}()

	svc.log.Debugf("MgmtSvc.PoolCreate forwarding modified req:%+v\n", req)
	dresp, err := svc.harness.CallDrpc(ctx, drpc.MethodPoolCreate, req)
	if err != nil {
		return nil, err
	}

	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolCreate response")
	}

	if resp.GetStatus() != 0 {
		if err := svc.sysdb.RemovePoolService(ps.PoolUUID); err != nil {
			return nil, err
		}
		return resp, nil
	}
	// let the caller know what was actually created
	resp.TgtRanks = req.GetRanks()
	resp.ScmBytes = req.Scmbytes
	resp.NvmeBytes = req.Nvmebytes

	ps.Replicas = system.RanksFromUint32(resp.GetSvcReps())
	ps.State = system.PoolServiceStateReady
	if err := svc.sysdb.UpdatePoolService(ps); err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.PoolCreate dispatch resp:%+v\n", resp)

	return resp, nil
}

// PoolResolveID implements a handler for resolving a user-friendly Pool ID into
// a UUID.
func (svc *mgmtSvc) PoolResolveID(ctx context.Context, req *mgmtpb.PoolResolveIDReq) (*mgmtpb.PoolResolveIDResp, error) {
	if err := svc.checkReplicaRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("MgmtSvc.PoolResolveID dispatch, req:%+v", req)

	if req.HumanID == "" {
		return nil, errors.New("empty request")
	}

	resp := new(mgmtpb.PoolResolveIDResp)
	type lookupFn func(string) (*system.PoolService, error)
	// Cycle through a list of lookup functions, returning the first one
	// that succeeds in finding the pool, or an error if no pool is found.
	for _, lookup := range []lookupFn{svc.sysdb.FindPoolServiceByLabel} {
		ps, err := lookup(req.HumanID)
		if err == nil {
			resp.Uuid = ps.PoolUUID.String()
			break
		}
	}

	if resp.Uuid == "" {
		return nil, errors.Errorf("unable to find pool service with human-friendly id %q", req.HumanID)
	}

	svc.log.Debugf("MgmtSvc.PoolResolveID dispatch, resp:%+v", resp)

	return resp, nil
}

// PoolDestroy implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolDestroy(ctx context.Context, req *mgmtpb.PoolDestroyReq) (*mgmtpb.PoolDestroyResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("MgmtSvc.PoolDestroy dispatch, req:%+v\n", req)

	uuid, err := uuid.Parse(req.GetUuid())
	if err != nil {
		return nil, err
	}

	// FIXME: There are some potential races here. We may want
	// to somehow do all of this under a lock, to prevent multiple
	// gRPC callers from modifying the same pool concurrently.

	ps, err := svc.sysdb.FindPoolServiceByUUID(uuid)
	if err != nil {
		return nil, err
	}

	lastState := ps.State
	if ps.State == system.PoolServiceStateDestroying {
		return nil, drpc.DaosAlready
	}

	ps.State = system.PoolServiceStateDestroying
	if err := svc.sysdb.UpdatePoolService(ps); err != nil {
		return nil, err
	}

	req.SvcRanks = system.RanksToUint32(ps.Replicas)
	dresp, err := svc.harness.CallDrpc(ctx, drpc.MethodPoolDestroy, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolDestroyResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolDestroy response")
	}

	svc.log.Debugf("MgmtSvc.PoolDestroy dispatch, resp:%+v\n", resp)

	switch drpc.DaosStatus(resp.Status) {
	case drpc.DaosSuccess:
		if err := svc.sysdb.RemovePoolService(uuid); err != nil {
			return nil, errors.Wrapf(err, "failed to remove pool %s", uuid)
		}
	// TODO: Identify errors that should leave the pool in a "Destroying"
	// state and enumerate them here.
	default:
		// Revert the pool back to the previous state if the destroy failed
		// and the pool should not remain in the "Destroying" state.
		ps.State = lastState
		if err := svc.sysdb.UpdatePoolService(ps); err != nil {
			return nil, err
		}
	}

	return resp, nil
}

// PoolEvict implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolEvict(ctx context.Context, req *mgmtpb.PoolEvictReq) (*mgmtpb.PoolEvictResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("MgmtSvc.PoolEvict dispatch, req:%+v\n", req)

	dresp, err := svc.makePoolServiceCall(ctx, drpc.MethodPoolEvict, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolEvictResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolEvict response")
	}

	svc.log.Debugf("MgmtSvc.PoolEvict dispatch, resp:%+v\n", resp)

	return resp, nil
}

// PoolExclude implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolExclude(ctx context.Context, req *mgmtpb.PoolExcludeReq) (*mgmtpb.PoolExcludeResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("MgmtSvc.PoolExclude dispatch, req:%+v\n", req)

	dresp, err := svc.makePoolServiceCall(ctx, drpc.MethodPoolExclude, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolExcludeResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolExclude response")
	}

	svc.log.Debugf("MgmtSvc.PoolExclude dispatch, resp:%+v\n", resp)

	return resp, nil
}

// PoolDrain implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolDrain(ctx context.Context, req *mgmtpb.PoolDrainReq) (*mgmtpb.PoolDrainResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("MgmtSvc.PoolDrain dispatch, req:%+v\n", req)

	dresp, err := svc.makePoolServiceCall(ctx, drpc.MethodPoolDrain, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolDrainResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolDrain response")
	}

	svc.log.Debugf("MgmtSvc.PoolDrain dispatch, resp:%+v\n", resp)

	return resp, nil
}

// PoolExtend implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolExtend(ctx context.Context, req *mgmtpb.PoolExtendReq) (*mgmtpb.PoolExtendResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.PoolExtend dispatch, req:%+v\n", req)

	// the IO server needs the domain tree for placement purposes
	req.FaultDomains = svc.sysdb.FaultDomainTree().ToProto()

	svc.log.Debugf("MgmtSvc.PoolExtend forwarding modified req:%+v\n", req)

	dresp, err := svc.makePoolServiceCall(ctx, drpc.MethodPoolExtend, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolExtendResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolExtend response")
	}

	svc.log.Debugf("MgmtSvc.PoolExtend dispatch, resp:%+v\n", resp)

	return resp, nil
}

// PoolReintegrate implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolReintegrate(ctx context.Context, req *mgmtpb.PoolReintegrateReq) (*mgmtpb.PoolReintegrateResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("MgmtSvc.PoolReintegrate dispatch, req:%+v\n", req)

	dresp, err := svc.makePoolServiceCall(ctx, drpc.MethodPoolReintegrate, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolReintegrateResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolReintegrate response")
	}

	svc.log.Debugf("MgmtSvc.PoolReintegrate dispatch, resp:%+v\n", resp)

	return resp, nil
}

// PoolQuery forwards a pool query request to the I/O server.
func (svc *mgmtSvc) PoolQuery(ctx context.Context, req *mgmtpb.PoolQueryReq) (*mgmtpb.PoolQueryResp, error) {
	if err := svc.checkReplicaRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("MgmtSvc.PoolQuery dispatch, req:%+v\n", req)

	dresp, err := svc.makePoolServiceCall(ctx, drpc.MethodPoolQuery, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolQueryResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolQuery response")
	}

	svc.log.Debugf("MgmtSvc.PoolQuery dispatch, resp:%+v\n", resp)

	return resp, nil
}

// resolvePoolPropVal resolves string-based property names and values to their C equivalents.
func resolvePoolPropVal(req *mgmtpb.PoolSetPropReq) (*mgmtpb.PoolSetPropReq, error) {
	newReq := &mgmtpb.PoolSetPropReq{
		Uuid: req.Uuid,
	}

	propName := strings.TrimSpace(req.GetName())
	switch strings.ToLower(propName) {
	case "reclaim":
		newReq.SetPropertyNumber(drpc.PoolPropertySpaceReclaim)

		recType := strings.TrimSpace(req.GetStrval())
		switch strings.ToLower(recType) {
		case "disabled":
			newReq.SetValueNumber(drpc.PoolSpaceReclaimDisabled)
		case "lazy":
			newReq.SetValueNumber(drpc.PoolSpaceReclaimLazy)
		case "time":
			newReq.SetValueNumber(drpc.PoolSpaceReclaimTime)
		default:
			return nil, errors.Errorf("unhandled reclaim type %q", recType)
		}
	case "label":
		newReq.SetPropertyNumber(drpc.PoolPropertyLabel)
		newReq.SetValueString(req.GetStrval())
	case "space_rb":
		newReq.SetPropertyNumber(drpc.PoolPropertyReservedSpace)

		if strVal := req.GetStrval(); strVal != "" {
			return nil, errors.Errorf("invalid space_rb value %q (valid values: 0-100)", strVal)
		}

		rsPct := req.GetNumval()
		if rsPct > 100 {
			return nil, errors.Errorf("invalid space_rb value %d (valid values: 0-100)", rsPct)
		}
		newReq.SetValueNumber(rsPct)
	case "self_heal":
		newReq.SetPropertyNumber(drpc.PoolPropertySelfHealing)

		healType := strings.TrimSpace(req.GetStrval())
		switch strings.ToLower(healType) {
		case "exclude":
			newReq.SetValueNumber(drpc.PoolSelfHealingAutoExclude)
		case "rebuild":
			newReq.SetValueNumber(drpc.PoolSelfHealingAutoRebuild)
		default:
			return nil, errors.Errorf("unhandled self_heal type %q", healType)
		}
	default:
		return nil, errors.Errorf("unhandled pool property %q", propName)
	}

	return newReq, nil
}

// PoolSetProp forwards a request to the I/O server to set a pool property.
func (svc *mgmtSvc) PoolSetProp(ctx context.Context, req *mgmtpb.PoolSetPropReq) (*mgmtpb.PoolSetPropResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("MgmtSvc.PoolSetProp dispatch, req:%+v", req)

	newReq, err := resolvePoolPropVal(req)
	if err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.PoolSetProp dispatch, req (converted):%+v", newReq)

	// Label is a special case, in that we need to ensure that it's unique
	// and also to update the pool service entry.
	if newReq.GetNumber() == drpc.PoolPropertyLabel {
		uuidStr := newReq.GetUuid()
		label := newReq.GetStrval()

		uuid, err := uuid.Parse(uuidStr)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to parse request uuid %q", uuidStr)
		}
		ps, err := svc.sysdb.FindPoolServiceByUUID(uuid)
		if err != nil {
			return nil, err
		}

		if label != "" {
			// If we're setting a label, first check to see
			// if a pool has already had the label applied.
			found, err := svc.sysdb.FindPoolServiceByLabel(label)
			if found != nil && found.PoolUUID != ps.PoolUUID {
				// If we find a pool with this label but the
				// UUID differs, then we should fail the request.
				return nil, FaultPoolDuplicateLabel(label)
			}
			if err != nil && !system.IsPoolNotFound(err) {
				// If the query failed, then we should fail
				// the request.
				return nil, err
			}
			// Otherwise, allow the label to be set again on the same
			// pool for idempotency.
		}

		defer func() {
			if ps == nil || err != nil {
				return
			}

			// Persist the label update in the MS DB if the
			// dRPC call succeeded.
			ps.PoolLabel = label
			err = svc.sysdb.UpdatePoolService(ps)
		}()
	}

	var dresp *drpc.Response
	dresp, err = svc.makePoolServiceCall(ctx, drpc.MethodPoolSetProp, newReq)
	if err != nil {
		return nil, err
	}

	resp := new(mgmtpb.PoolSetPropResp)
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolSetProp response")
	}

	svc.log.Debugf("MgmtSvc.PoolSetProp dispatch, resp:%+v", resp)

	if resp.GetStatus() != 0 {
		return resp, nil
	}

	if resp.GetNumber() != newReq.GetNumber() {
		return nil, errors.Errorf("Response number doesn't match request (%d != %d)",
			resp.GetNumber(), newReq.GetNumber())
	}
	// Restore the string versions of the property/value
	resp.Property = &mgmtpb.PoolSetPropResp_Name{
		Name: req.GetName(),
	}
	if req.GetStrval() != "" {
		if resp.GetNumval() != newReq.GetNumval() {
			return nil, errors.Errorf("Response value doesn't match request (%d != %d)",
				resp.GetNumval(), newReq.GetNumval())
		}
		resp.Value = &mgmtpb.PoolSetPropResp_Strval{
			Strval: req.GetStrval(),
		}
	}

	return resp, nil
}

// PoolGetACL forwards a request to the IO server to fetch a pool's Access Control List
func (svc *mgmtSvc) PoolGetACL(ctx context.Context, req *mgmtpb.GetACLReq) (*mgmtpb.ACLResp, error) {
	if err := svc.checkReplicaRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("MgmtSvc.PoolGetACL dispatch, req:%+v\n", req)

	dresp, err := svc.makePoolServiceCall(ctx, drpc.MethodPoolGetACL, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ACLResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolGetACL response")
	}

	svc.log.Debugf("MgmtSvc.PoolGetACL dispatch, resp:%+v\n", resp)

	return resp, nil
}

// PoolOverwriteACL forwards a request to the IO server to overwrite a pool's Access Control List
func (svc *mgmtSvc) PoolOverwriteACL(ctx context.Context, req *mgmtpb.ModifyACLReq) (*mgmtpb.ACLResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("MgmtSvc.PoolOverwriteACL dispatch, req:%+v\n", req)

	dresp, err := svc.makePoolServiceCall(ctx, drpc.MethodPoolOverwriteACL, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ACLResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolOverwriteACL response")
	}

	svc.log.Debugf("MgmtSvc.PoolOverwriteACL dispatch, resp:%+v\n", resp)

	return resp, nil
}

// PoolUpdateACL forwards a request to the IO server to add or update entries in
// a pool's Access Control List
func (svc *mgmtSvc) PoolUpdateACL(ctx context.Context, req *mgmtpb.ModifyACLReq) (*mgmtpb.ACLResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("MgmtSvc.PoolUpdateACL dispatch, req:%+v\n", req)

	dresp, err := svc.makePoolServiceCall(ctx, drpc.MethodPoolUpdateACL, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ACLResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolUpdateACL response")
	}

	svc.log.Debugf("MgmtSvc.PoolUpdateACL dispatch, resp:%+v\n", resp)

	return resp, nil
}

// PoolDeleteACL forwards a request to the IO server to delete an entry from a
// pool's Access Control List.
func (svc *mgmtSvc) PoolDeleteACL(ctx context.Context, req *mgmtpb.DeleteACLReq) (*mgmtpb.ACLResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("MgmtSvc.PoolDeleteACL dispatch, req:%+v\n", req)

	dresp, err := svc.makePoolServiceCall(ctx, drpc.MethodPoolDeleteACL, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ACLResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolDeleteACL response")
	}

	svc.log.Debugf("MgmtSvc.PoolDeleteACL dispatch, resp:%+v\n", resp)

	return resp, nil
}

// ListPools returns a set of all pools in the system.
func (svc *mgmtSvc) ListPools(ctx context.Context, req *mgmtpb.ListPoolsReq) (*mgmtpb.ListPoolsResp, error) {
	if err := svc.checkReplicaRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("MgmtSvc.ListPools dispatch, req:%+v\n", req)

	psList, err := svc.sysdb.PoolServiceList()
	if err != nil {
		return nil, err
	}

	resp := new(mgmtpb.ListPoolsResp)
	for _, ps := range psList {
		resp.Pools = append(resp.Pools, &mgmtpb.ListPoolsResp_Pool{
			Uuid:    ps.PoolUUID.String(),
			SvcReps: system.RanksToUint32(ps.Replicas),
		})
	}

	svc.log.Debugf("MgmtSvc.ListPools dispatch, resp:%+v\n", resp)

	return resp, nil
}
