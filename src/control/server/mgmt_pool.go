//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"math/rand"
	"sort"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"
	"golang.org/x/net/context"
	"google.golang.org/protobuf/proto"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	// DefaultPoolScmRatio defines the default SCM:NVMe ratio for
	// requests that do not specify one.
	DefaultPoolScmRatio = 0.06
	// DefaultPoolNvmeRatio defines the default NVMe:SCM ratio for
	// requests that do not specify one.
	DefaultPoolNvmeRatio = 0.94
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
	GetId() string
	SetUUID(uuid.UUID)
	GetSvcRanks() []uint32
	SetSvcRanks(rl []uint32)
}

func (svc *mgmtSvc) makePoolServiceCall(ctx context.Context, method drpc.Method, req poolServiceReq) (*drpc.Response, error) {
	ps, err := svc.getPoolService(req.GetId())
	if err != nil {
		return nil, err
	}
	req.SetUUID(ps.PoolUUID)

	if len(req.GetSvcRanks()) == 0 {
		rl, err := svc.getPoolServiceRanks(ps)
		if err != nil {
			return nil, err
		}
		req.SetSvcRanks(rl)
	}

	return svc.harness.CallDrpc(ctx, method, req)
}

// resolvePoolID implements a handler for resolving a user-friendly Pool ID into
// a UUID.
func (svc *mgmtSvc) resolvePoolID(id string) (uuid.UUID, error) {
	if id == "" {
		return uuid.Nil, errors.New("empty pool id")
	}

	if out, err := uuid.Parse(id); err == nil {
		return out, nil
	}

	type lookupFn func(string) (*system.PoolService, error)
	// Cycle through a list of lookup functions, returning the first one
	// that succeeds in finding the pool, or an error if no pool is found.
	for _, lookup := range []lookupFn{svc.sysdb.FindPoolServiceByLabel} {
		ps, err := lookup(id)
		if err == nil {
			return ps.PoolUUID, nil
		}
	}

	return uuid.Nil, errors.Errorf("unable to find pool with label %q", id)
}

// getPoolService returns the pool service entry for the given UUID.
func (svc *mgmtSvc) getPoolService(id string) (*system.PoolService, error) {
	uuid, err := svc.resolvePoolID(id)
	if err != nil {
		return nil, err
	}

	ps, err := svc.sysdb.FindPoolServiceByUUID(uuid)
	if err != nil {
		return nil, err
	}

	if ps.State != system.PoolServiceStateReady {
		return nil, drpc.DaosTryAgain
	}

	return ps, nil
}

// getPoolServiceRanks returns a slice of ranks designated as the
// pool service hosts.
func (svc *mgmtSvc) getPoolServiceRanks(ps *system.PoolService) ([]uint32, error) {
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
		return nil, errors.Errorf("unable to find any available service ranks for pool %s", ps.PoolUUID)
	}

	return system.RanksToUint32(readyRanks), nil
}

// calculateCreateStorage determines the amount of SCM/NVMe storage to
// allocate per engine in order to fulfill the create request, if those
// values are not already supplied as part of the request.
func (svc *mgmtSvc) calculateCreateStorage(req *mgmtpb.PoolCreateReq) error {
	instances := svc.harness.Instances()
	if len(instances) < 1 {
		return errors.New("harness has no managed instances")
	}

	if len(req.GetRanks()) == 0 {
		return errors.New("zero ranks in calculateCreateStorage()")
	}

	// the engine will accept only 2 tiers - add missing
	if len(req.GetTierratio()) == 0 {
		req.Tierratio = []float64{DefaultPoolScmRatio, DefaultPoolNvmeRatio}
	} else if len(req.GetTierratio()) == 1 {
		req.Tierratio = append(req.Tierratio, 0)
	}

	storagePerRank := func(total uint64) uint64 {
		return total / uint64(len(req.GetRanks()))
	}

	if len(req.Tierbytes) == 0 {
		req.Tierbytes = make([]uint64, len(req.Tierratio))
	} else if len(req.Tierbytes) == 1 {
		req.Tierbytes = append(req.Tierbytes, 0)
	}

	switch {
	case !instances[0].HasBlockDevices():
		svc.log.Info("config has 0 bdevs; excluding NVMe from pool create request")
		for tierIdx := range req.Tierbytes {
			if tierIdx > 0 {
				req.Tierbytes[tierIdx] = 0
			} else if req.Tierbytes[0] == 0 {
				req.Tierbytes[0] = storagePerRank(req.GetTotalbytes())
			}
		}
	case req.GetTotalbytes() > 0:
		for tierIdx := range req.Tierbytes {
			req.Tierbytes[tierIdx] = storagePerRank(uint64(float64(req.GetTotalbytes()) * req.Tierratio[tierIdx]))
		}
	}

	// zero these out as they're not needed anymore
	req.Totalbytes = 0
	for tierIdx := range req.Tierratio {
		req.Tierratio[tierIdx] = 0
	}

	targetCount := instances[0].GetTargetCount()
	if targetCount == 0 {
		return errors.New("zero target count")
	}
	minNvmeRequired := engine.NvmeMinBytesPerTarget * uint64(targetCount)

	if req.Tierbytes[1] != 0 && req.Tierbytes[1] < minNvmeRequired {
		return FaultPoolNvmeTooSmall(req.Tierbytes[1], targetCount)
	}
	if req.Tierbytes[0] < engine.ScmMinBytesPerTarget*uint64(targetCount) {
		return FaultPoolScmTooSmall(req.Tierbytes[0], targetCount)
	}

	return nil
}

// PoolCreate implements the method defined for the Management Service.
//
// Validate minimum SCM/NVMe pool size per VOS target, pool size request params
// are per-engine so need to be larger than (minimum_target_allocation *
// target_count).
func (svc *mgmtSvc) PoolCreate(ctx context.Context, req *mgmtpb.PoolCreateReq) (resp *mgmtpb.PoolCreateResp, err error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.PoolCreate dispatch, req:%s\n", mgmtpb.Debug(req))

	uuid, err := uuid.Parse(req.GetUuid())
	if err != nil {
		return nil, errors.Wrapf(err, "failed to parse pool UUID %q", req.GetUuid())
	}

	resp = new(mgmtpb.PoolCreateResp)
	ps, err := svc.sysdb.FindPoolServiceByUUID(uuid)
	if ps != nil {
		svc.log.Debugf("found pool %s state=%s", ps.PoolUUID, ps.State)
		resp.Status = int32(drpc.DaosAlready)
		if ps.State != system.PoolServiceStateReady {
			resp.Status = int32(drpc.DaosTryAgain)
			return resp, svc.checkPools(ctx, ps)
		}
		return resp, nil
	}
	if _, ok := err.(*system.ErrPoolNotFound); !ok {
		return nil, err
	}

	labelExists := false
	var poolLabel string
	for _, prop := range req.GetProperties() {
		if prop.Number != drpc.PoolPropertyLabel {
			continue
		}

		poolLabel = prop.GetStrval()
		if poolLabel == "" {
			break
		}

		labelExists = true
		if _, err := svc.sysdb.FindPoolServiceByLabel(poolLabel); err == nil {
			return nil, FaultPoolDuplicateLabel(poolLabel)
		}
	}

	if !labelExists {
		return nil, FaultPoolNoLabel
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

	// Clamp the maximum allowed svc replicas to the smaller of requested
	// storage ranks or MaxPoolServiceReps.
	maxSvcReps := func(allRanks int) uint32 {
		if allRanks > MaxPoolServiceReps {
			return uint32(MaxPoolServiceReps)
		}
		return uint32(allRanks)
	}(len(req.GetRanks()))

	// Set the number of service replicas to a reasonable default
	// if the request didn't specify. Note that the number chosen
	// should not be even in order to work best with the raft protocol's
	// 2N+1 resiliency model.
	if req.GetNumsvcreps() == 0 {
		req.Numsvcreps = DefaultPoolServiceReps
		if len(req.GetRanks()) < DefaultPoolServiceReps {
			req.Numsvcreps = 1
		}
	} else if req.GetNumsvcreps() > maxSvcReps {
		return nil, FaultPoolInvalidServiceReps(maxSvcReps)
	}

	// IO engine needs the fault domain tree for placement purposes
	req.FaultDomains, err = svc.membership.CompressedFaultDomainTree(req.Ranks...)
	if err != nil {
		return nil, err
	}

	if err := svc.calculateCreateStorage(req); err != nil {
		return nil, err
	}

	ps = system.NewPoolService(uuid, req.Tierbytes, system.RanksFromUint32(req.GetRanks()))
	ps.PoolLabel = poolLabel
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
					Id:       req.Uuid,
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

	svc.log.Debugf("MgmtSvc.PoolCreate forwarding modified req:%s\n", mgmtpb.Debug(req))
	dresp, err := svc.harness.CallDrpc(ctx, drpc.MethodPoolCreate, req)
	if err != nil {
		svc.log.Errorf("pool create dRPC call failed: %s", err)
		if err := svc.sysdb.RemovePoolService(ps.PoolUUID); err != nil {
			return nil, err
		}

		switch errors.Cause(err) {
		case errInstanceNotReady, FaultDataPlaneNotStarted:
			// If the pool create failed because there was no available instance
			// to service the request, signal to the client that it should try again.
			resp.Status = int32(drpc.DaosTryAgain)
			return resp, nil
		default:
			return nil, err
		}
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
	resp.TierBytes = make([]uint64, 2)
	resp.TierBytes[0] = req.Tierbytes[0]
	resp.TierBytes[1] = req.Tierbytes[1]

	ps.Replicas = system.RanksFromUint32(resp.GetSvcReps())
	ps.State = system.PoolServiceStateReady
	if err := svc.sysdb.UpdatePoolService(ps); err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.PoolCreate dispatch resp:%s\n", mgmtpb.Debug(resp))

	return resp, nil
}

// checkPools iterates over the list of pools in the system to check
// for any that are in an unexpected state. Pools not in the Ready
// state will be cleaned up and removed from the system.
func (svc *mgmtSvc) checkPools(ctx context.Context, psList ...*system.PoolService) error {
	if err := svc.sysdb.CheckLeader(); err != nil {
		return err
	}

	var err error
	if len(psList) == 0 {
		psList, err = svc.sysdb.PoolServiceList(true)
		if err != nil {
			return errors.Wrap(err, "failed to fetch pool service list")
		}
	}

	svc.log.Debugf("checking %d pools", len(psList))
	for _, ps := range psList {
		if ps.State == system.PoolServiceStateReady {
			continue
		}

		svc.log.Errorf("pool %s is in unexpected state %s", ps.PoolUUID, ps.State)

		// Change the pool state to Destroying in order to trigger
		// the cleanup mode of PoolDestroy(), which will cause the
		// destroy RPC to be sent to all ranks and then the service
		// will be removed from the system.
		if ps.State != system.PoolServiceStateDestroying {
			ps.State = system.PoolServiceStateDestroying
			if err := svc.sysdb.UpdatePoolService(ps); err != nil {
				return errors.Wrapf(err, "failed to update pool %s", ps.PoolUUID)
			}
		}

		// Attempt to destroy the pool.
		dr := &mgmtpb.PoolDestroyReq{
			Sys:   svc.sysdb.SystemName(),
			Force: true,
			Id:    ps.PoolUUID.String(),
		}

		_, err := svc.PoolDestroy(ctx, dr)
		if err != nil {
			svc.log.Errorf("error while destroying pool %s: %s", ps.PoolUUID, err)
		}
	}

	return nil
}

// PoolDestroy implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolDestroy(ctx context.Context, req *mgmtpb.PoolDestroyReq) (*mgmtpb.PoolDestroyResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("MgmtSvc.PoolDestroy dispatch, req:%+v\n", req)

	uuid, err := svc.resolvePoolID(req.Id)
	if err != nil {
		return nil, err
	}

	ps, err := svc.sysdb.FindPoolServiceByUUID(uuid)
	if err != nil {
		return nil, err
	}
	req.SetUUID(uuid)

	resp := &mgmtpb.PoolDestroyResp{}
	inCleanupMode := false
	if ps.State == system.PoolServiceStateDestroying {
		// If we already tried to destroy the pool but it failed for some
		// reason, try again, but instead use the full set of storage ranks
		// in case the MS has lost track of the actual svc ranks.
		req.SvcRanks = system.RanksToUint32(ps.Storage.CreationRanks())
		inCleanupMode = true
	} else {
		req.SvcRanks = system.RanksToUint32(ps.Replicas)

		// Perform separate PoolEvict _before_ possible transition to destroying state.
		evreq := &mgmtpb.PoolEvictReq{}
		evreq.Sys = req.Sys
		evreq.Id = req.Id
		evreq.SvcRanks = req.SvcRanks
		evreq.Destroy = true
		evreq.ForceDestroy = req.Force
		svc.log.Debugf("MgmtSvc.PoolDestroy issuing drpc.MethodPoolEvict, evreq:%+v\n", evreq)
		evresp, err := svc.PoolEvict(ctx, evreq)
		if err != nil {
			svc.log.Debugf("svc.PoolEvict failed\n")
			return nil, err
		}
		ds := drpc.DaosStatus(evresp.Status)
		svc.log.Debugf("MgmtSvc.PoolDestroy drpc.MethodPoolEvict, evresp:%+v\n", evresp)

		// If the destroy request is being forced, we should additionally zap the label
		// so the entry doesn't prevent a new pool with the same label from being created.
		if req.Force {
			ps.PoolLabel = ""
		}

		// If the request is being forced, or the evict request did not fail
		// due to the pool being busy, then transition to the destroying state
		// and persist the update(s).
		if req.Force || ds != drpc.DaosBusy {
			ps.State = system.PoolServiceStateDestroying
			if err := svc.sysdb.UpdatePoolService(ps); err != nil {
				return nil, errors.Wrapf(err, "failed to update pool %s", uuid)
			}
		}

		if ds != drpc.DaosSuccess {
			svc.log.Errorf("PoolEvict (first step of destroy) failed: %s", ds)
			resp.Status = int32(ds)
			return resp, nil
		}
	}

	// Now on to the rest of the pool destroy, issue drpc.MethodPoolDestroy.
	svc.log.Debugf("MgmtSvc.PoolDestroy issuing drpc.MethodPoolDestroy, req:%+v\n", req)
	dresp, err := svc.harness.CallDrpc(ctx, drpc.MethodPoolDestroy, req)
	if err != nil {
		return nil, err
	}

	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolDestroy response")
	}

	svc.log.Debugf("MgmtSvc.PoolDestroy dispatch, resp:%+v\n", resp)

	ds := drpc.DaosStatus(resp.Status)
	switch ds {
	case drpc.DaosSuccess, drpc.DaosNotLeader, drpc.DaosNotReplica:
		if ds == drpc.DaosNotLeader || ds == drpc.DaosNotReplica {
			// If we're not cleaning up, then this is an error.
			// Note: Unlikely to see !inCleanupMode (evict would have seen first?)
			if !inCleanupMode {
				svc.log.Errorf("PoolDestroy dRPC call failed due to %s in non-cleanup path", ds)
				break
			}
			// Otherwise, we've done all we can to try to recover.
			resp.Status = int32(drpc.DaosSuccess)
		}
		if err := svc.sysdb.RemovePoolService(uuid); err != nil {
			return nil, errors.Wrapf(err, "failed to remove pool %s", uuid)
		}
	default:
		svc.log.Errorf("PoolDestroy dRPC call failed: %s", ds)
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

	// the IO engine needs the domain tree for placement purposes
	fdTree, err := svc.membership.CompressedFaultDomainTree(req.Ranks...)
	if err != nil {
		return nil, err
	}
	req.FaultDomains = fdTree

	// Look up the pool service record to find the storage allocations
	// used at creation.
	ps, err := svc.getPoolService(req.GetId())
	if err != nil {
		return nil, err
	}
	req.Tierbytes = ps.Storage.PerRankTierStorage

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

// PoolQuery forwards a pool query request to the I/O Engine.
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

func (svc *mgmtSvc) updatePoolLabel(ctx context.Context, sys string, uuid uuid.UUID, prop *mgmtpb.PoolProperty) error {
	if prop.GetNumber() != drpc.PoolPropertyLabel {
		return errors.New("updatePoolLabel() called with non-label prop")
	}
	label := prop.GetStrval()

	ps, err := svc.sysdb.FindPoolServiceByUUID(uuid)
	if err != nil {
		return err
	}

	if label != "" {
		// If we're setting a label, first check to see
		// if a pool has already had the label applied.
		found, err := svc.sysdb.FindPoolServiceByLabel(label)
		if found != nil && found.PoolUUID != ps.PoolUUID {
			// If we find a pool with this label but the
			// UUID differs, then we should fail the request.
			return FaultPoolDuplicateLabel(label)
		}
		if err != nil && !system.IsPoolNotFound(err) {
			// If the query failed, then we should fail
			// the request.
			return err
		}
		// Otherwise, allow the label to be set again on the same
		// pool for idempotency.
	}

	req := &mgmtpb.PoolSetPropReq{
		Sys:        sys,
		Id:         uuid.String(),
		Properties: []*mgmtpb.PoolProperty{prop},
	}

	var dresp *drpc.Response
	dresp, err = svc.makePoolServiceCall(ctx, drpc.MethodPoolSetProp, req)
	if err != nil {
		return err
	}

	resp := new(mgmtpb.PoolSetPropResp)
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return errors.Wrap(err, "unmarshal PoolSetProp response")
	}

	if resp.GetStatus() != 0 {
		return errors.Errorf("label update failed: %s", drpc.Status(resp.Status))
	}

	// Persist the label update in the MS DB if the
	// dRPC call succeeded.
	ps.PoolLabel = label
	return svc.sysdb.UpdatePoolService(ps)
}

// PoolSetProp forwards a request to the I/O Engine to set pool properties.
func (svc *mgmtSvc) PoolSetProp(ctx context.Context, req *mgmtpb.PoolSetPropReq) (*mgmtpb.PoolSetPropResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("MgmtSvc.PoolSetProp dispatch, req:%+v", req)

	uuid, err := svc.resolvePoolID(req.GetId())
	if err != nil {
		return nil, err
	}

	if len(req.GetProperties()) == 0 {
		return nil, errors.New("PoolSetProp() request with 0 properties")
	}

	miscProps := make([]*mgmtpb.PoolProperty, 0, len(req.GetProperties()))
	for _, prop := range req.GetProperties() {
		// Label is a special case, in that we need to ensure that it's unique
		// and also to update the pool service entry. Handle it first and separately
		// so that if it fails, none of the other props are changed.
		if prop.GetNumber() == drpc.PoolPropertyLabel {
			if err := svc.updatePoolLabel(ctx, req.GetSys(), uuid, prop); err != nil {
				return nil, err
			}
			continue
		}

		miscProps = append(miscProps, prop)
	}

	resp := new(mgmtpb.PoolSetPropResp)
	if len(miscProps) == 0 {
		return resp, nil
	}

	req.Properties = miscProps

	var dresp *drpc.Response
	dresp, err = svc.makePoolServiceCall(ctx, drpc.MethodPoolSetProp, req)
	if err != nil {
		return nil, err
	}

	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolSetProp response")
	}

	svc.log.Debugf("MgmtSvc.PoolSetProp dispatch, resp:%+v", resp)

	return resp, nil
}

// PoolGetProp forwards a request to the I/O Engine to get pool properties.
func (svc *mgmtSvc) PoolGetProp(ctx context.Context, req *mgmtpb.PoolGetPropReq) (*mgmtpb.PoolGetPropResp, error) {
	if err := svc.checkReplicaRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("MgmtSvc.PoolGetProp dispatch, req:%+v", req)

	// The request must contain a list of expected properties. We don't want
	// to just let the engine return all properties because not all properties
	// are valid to retrieve this way (e.g. ACL, etc).
	if len(req.GetProperties()) == 0 {
		return nil, errors.Errorf("PoolGetProp() request with 0 properties")
	}

	dresp, err := svc.makePoolServiceCall(ctx, drpc.MethodPoolGetProp, req)
	if err != nil {
		return nil, err
	}

	resp := new(mgmtpb.PoolGetPropResp)
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolGetProp response")
	}

	svc.log.Debugf("MgmtSvc.PoolGetProp dispatch, resp: %+v", resp)

	if resp.GetStatus() != 0 {
		return resp, nil
	}

	return resp, nil
}

// PoolGetACL forwards a request to the I/O Engine to fetch a pool's Access Control List
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

// PoolOverwriteACL forwards a request to the I/O Engine to overwrite a pool's Access Control List
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

// PoolUpdateACL forwards a request to the I/O Engine to add or update entries in
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

// PoolDeleteACL forwards a request to the I/O Engine to delete an entry from a
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

	psList, err := svc.sysdb.PoolServiceList(false)
	if err != nil {
		return nil, err
	}

	resp := new(mgmtpb.ListPoolsResp)
	for _, ps := range psList {
		resp.Pools = append(resp.Pools, &mgmtpb.ListPoolsResp_Pool{
			Uuid:    ps.PoolUUID.String(),
			Label:   ps.PoolLabel,
			SvcReps: system.RanksToUint32(ps.Replicas),
		})
	}

	svc.log.Debugf("MgmtSvc.ListPools dispatch, resp:%+v\n", resp)

	return resp, nil
}
