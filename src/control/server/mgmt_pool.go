//
// (C) Copyright 2020-2024 Intel Corporation.
// (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"math/rand"
	"sort"
	"time"

	"github.com/dustin/go-humanize"
	"github.com/google/uuid"
	"github.com/pkg/errors"
	"golang.org/x/net/context"
	"google.golang.org/protobuf/proto"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

/*
#include <stdint.h>

#include <daos_pool.h>
*/
import "C"

const (
	// DefaultPoolScmRatio defines the default SCM:NVMe ratio for
	// requests that do not specify one.
	DefaultPoolScmRatio = 0.06
	// DefaultPoolNvmeRatio defines the default NVMe:SCM ratio for
	// requests that do not specify one.
	DefaultPoolNvmeRatio = 0.94
	// MaxPoolServiceReps defines the maximum number of pool service
	// replicas that may be configured when creating a pool.
	MaxPoolServiceReps = 2*daos.PoolSvcRedunFacMax + 1
)

type poolServiceReq interface {
	proto.Message
	GetId() string
	SetUUID(uuid.UUID)
	GetSvcRanks() []uint32
	SetSvcRanks(rl []uint32)
}

func (svc *mgmtSvc) makeLockedPoolServiceCall(ctx context.Context, method drpc.Method, req poolServiceReq) (*drpc.Response, error) {
	ps, err := svc.getPoolService(req.GetId())
	if err != nil {
		return nil, err
	}
	lock, err := svc.sysdb.TakePoolLock(ctx, ps.PoolUUID)
	if err != nil {
		return nil, err
	}
	defer lock.Release()

	return svc.makePoolServiceCall(lock.InContext(ctx), method, req)
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

	return uuid.Nil, system.ErrPoolLabelNotFound(id)
}

// getPoolService returns the pool service entry for the given UUID.
func (svc *mgmtSvc) getPoolService(id string) (*system.PoolService, error) {
	poolUUID, err := svc.resolvePoolID(id)
	if err != nil {
		return nil, err
	}

	ps, err := svc.sysdb.FindPoolServiceByUUID(poolUUID)
	if err != nil {
		return nil, err
	}

	if ps.State != system.PoolServiceStateReady {
		return nil, daos.TryAgain
	}

	return ps, nil
}

// getPoolServiceRanks returns a slice of ranks designated as the
// pool service hosts.
func (svc *mgmtSvc) getPoolServiceRanks(ps *system.PoolService) ([]uint32, error) {
	readyRanks := make([]ranklist.Rank, 0, len(ps.Replicas))
	for _, r := range ps.Replicas {
		m, err := svc.sysdb.FindMemberByRank(r)
		if err != nil {
			return nil, err
		}
		if m.State&system.AvailableMemberFilter == 0 {
			continue
		}
		readyRanks = append(readyRanks, r)
	}

	if len(readyRanks) == 0 {
		return nil, errors.Errorf("unable to find any available service ranks for pool %s", ps.PoolUUID)
	}

	return ranklist.RanksToUint32(readyRanks), nil
}

func minRankScm(tgtCount uint64) uint64 {
	return tgtCount * engine.ScmMinBytesPerTarget
}

func minPoolScm(tgtCount, rankCount uint64) uint64 {
	return minRankScm(tgtCount) * rankCount
}

func minRankNvme(tgtCount uint64) uint64 {
	return tgtCount * engine.NvmeMinBytesPerTarget
}

func minPoolNvme(tgtCount, rankCount uint64) uint64 {
	return minRankNvme(tgtCount) * rankCount
}

// poolCreateActiveRankCount returns the number of ranks in req.Ranks that are
// not also present in req.UnavailableRanks (i.e. the ranks that will actually
// host storage for the pool). After PoolCreate's split logic the two lists are
// disjoint; the RankSet-based overlap check below is defensive so this helper
// stays correct if a future code path breaks that invariant.
func poolCreateActiveRankCount(req *mgmtpb.PoolCreateReq) int {
	downoutSet := ranklist.RankSetFromRanks(ranklist.RanksFromUint32(req.GetUnavailableRanks()))
	n := 0
	for _, r := range req.GetRanks() {
		if !downoutSet.Contains(ranklist.Rank(r)) {
			n++
		}
	}
	return n
}

// calculateCreateStorage determines the amount of SCM/NVMe storage to allocate per engine in order
// to fulfill the create request, if those values are not already supplied as part of the request.
func (svc *mgmtSvc) calculateCreateStorage(req *mgmtpb.PoolCreateReq) error {
	instances := svc.harness.Instances()
	if len(instances) < 1 {
		return errors.New("harness has no managed instances")
	}
	if len(req.GetRanks()) == 0 {
		return errors.New("zero ranks in calculateCreateStorage()")
	}

	mdOnSSD := instances[0].GetStorage().BdevRoleMetaConfigured()
	switch {
	case !mdOnSSD && req.MemRatio > 0:
		// Prevent MD-on-SSD parameters being used in incompatible mode.
		return FaultPoolMemRatioNoRoles
	case mdOnSSD && req.MemRatio == 0:
		// Set reasonable default if not set in MD-on-SSD mode.
		req.MemRatio = storage.DefaultMemoryFileRatio
		svc.log.Infof("Default memory-file:md-on-ssd ratio of %d%% applied",
			int(storage.DefaultMemoryFileRatio)*100)
	}

	// NB: The following logic is based on the assumption that a request will always include SCM
	// as tier 0. Currently, we only support one additional tier, NVMe, which is optional. As we
	// add support for other tiers, this logic will need to be updated.

	nvmeMissing := !instances[0].GetStorage().HasBlockDevices()

	// Determine the number of ranks that will actually host storage. DOWNOUT
	// ranks are normally passed separately from req.Ranks; only subtract any
	// overlap to handle explicit requests that included an excluded rank.
	activeRanks := poolCreateActiveRankCount(req)
	if activeRanks <= 0 {
		return errors.New("no active ranks for pool create")
	}

	// As this is an exclusive interface between control-API and server, accept only known
	// request parameter combinations.

	switch {
	// Pool tier sizes already specified in request.
	case len(req.TierBytes) == 2 && len(req.TierRatio) == 0 && req.TotalBytes == 0:
		// If no NVMe, refuse request as NVMe has been incorrectly requested.
		nvmeBytes := req.TierBytes[1]
		if nvmeMissing && nvmeBytes > 0 {
			return errors.Errorf("%s NVMe requested for pool but config has zero bdevs",
				humanize.IBytes(nvmeBytes))
		}

	// Pool tier sizes to be populated based on total-size and ratio.
	case len(req.TierBytes) == 0 && len(req.TierRatio) == 2 && req.TotalBytes > 0:
		// If no NVMe, adjust ratio as NVMe hasn't been specifically requested.
		if nvmeMissing {
			svc.log.Noticef("config has zero bdevs; excluding NVMe from pool create " +
				"request")
			req.TierRatio = []float64{1.00, 0.00}
		}
		req.TierBytes = make([]uint64, len(req.TierRatio))
		for tierIdx := range req.TierBytes {
			req.TierBytes[tierIdx] =
				uint64(float64(req.TotalBytes)*req.TierRatio[tierIdx]) /
					uint64(activeRanks)
			svc.log.Infof("%s = (%s*%f) / %d", humanize.IBytes(req.TierBytes[tierIdx]),
				humanize.IBytes(req.TotalBytes), req.TierRatio[tierIdx], activeRanks)
		}

	default:
		return errors.Errorf("unexpected pool create params in request: %+v", req)
	}

	// Sanity check tier bytes are greater than the minimums.
	tgts, ranks := uint64(instances[0].GetTargetCount()), uint64(activeRanks)
	if tgts == 0 {
		return errors.New("zero target count")
	}
	minPoolTotal := minPoolScm(tgts, ranks)
	if req.TierBytes[1] > 0 {
		minPoolTotal += minPoolNvme(tgts, ranks)
	}
	if req.TierBytes[0] < minRankScm(tgts) {
		return FaultPoolScmTooSmall(minPoolTotal, minPoolScm(tgts, ranks))
	}
	if req.TierBytes[1] != 0 && req.TierBytes[1] < minRankNvme(tgts) {
		return FaultPoolNvmeTooSmall(minPoolTotal, minPoolNvme(tgts, ranks))
	}

	// Zero no longer required request fields.
	req.TotalBytes = 0
	req.TierRatio = nil
	req.NumRanks = 0

	return nil
}

// PoolCreate implements the method defined for the Management Service.
//
// NB: Only one pool create request may be processed at a time.
func (svc *mgmtSvc) PoolCreate(ctx context.Context, req *mgmtpb.PoolCreateReq) (*mgmtpb.PoolCreateResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}

	msg, err := svc.submitSerialRequest(ctx, req)
	if err != nil {
		return nil, err
	}

	return msg.(*mgmtpb.PoolCreateResp), nil
}

// poolCreate handles the actual pool creation request. This is separated from
// PoolCreate() so that it can be called from the batch request handler.
//
// Validate minimum SCM/NVMe pool size per VOS target, pool size request params
// are per-engine so need to be larger than (minimum_target_allocation *
// target_count).
func (svc *mgmtSvc) poolCreate(parent context.Context, req *mgmtpb.PoolCreateReq) (resp *mgmtpb.PoolCreateResp, err error) {
	if err := svc.poolCreateAddSystemProps(req); err != nil {
		return nil, err
	}

	poolUUID, err := uuid.Parse(req.GetUuid())
	if err != nil {
		return nil, errors.Wrapf(err, "failed to parse pool UUID %q", req.GetUuid())
	}

	lock, err := svc.sysdb.TakePoolLock(parent, poolUUID)
	if err != nil {
		return nil, err
	}
	defer lock.Release()
	ctx := lock.InContext(parent)

	resp = new(mgmtpb.PoolCreateResp)
	ps, err := svc.sysdb.FindPoolServiceByUUID(poolUUID)
	if ps != nil {
		svc.log.Debugf("found pool %s state=%s", ps.PoolUUID, ps.State)
		if ps.State != system.PoolServiceStateReady {
			resp.Status = int32(daos.TryAgain)
			return resp, svc.checkPools(ctx, false, ps)
		}

		// If the pool is already created and is Ready, just return the existing pool info.
		// This can happen in the case of a retried PoolCreate after a leadership
		// shuffle that results in the pool being successfully created by the previous
		// gRPC handler which returned an error to the client after being unable to
		// persist the state update.
		qr, err := svc.PoolQuery(ctx, &mgmtpb.PoolQueryReq{Id: req.Uuid, Sys: req.Sys})
		if err != nil {
			return nil, errors.Wrap(err, "query on already-created pool failed")
		}

		resp.SvcLdr = qr.SvcLdr
		resp.SvcReps = ranklist.RanksToUint32(ps.Replicas)
		resp.TgtRanks = ranklist.RanksToUint32(ps.Storage.CreationRanks())
		resp.TierBytes = ps.Storage.PerRankTierStorage

		return resp, nil
	}
	if _, ok := err.(*system.ErrPoolNotFound); !ok {
		return nil, err
	}

	labelExists := false
	var poolLabel string
	for _, prop := range req.GetProperties() {
		if prop.Number != daos.PoolPropertyLabel {
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

	availableRanks, err := svc.sysdb.MemberRanks(system.AvailableMemberFilter)
	if err != nil {
		return nil, err
	}

	// Enumerate the ranks that must be recorded in the pool map as DOWNOUT rather than as
	// live storage targets. Only administratively/permanently excluded ranks qualify: a
	// DOWNOUT map entry can only be brought back with an explicit reintegration, so ranks
	// in transient states (Starting/Stopping/Stopped/Errored/Unresponsive/AwaitFormat) are
	// deliberately left out of the map entirely, exactly as before DAOS-18825.
	downoutRanks, err := svc.sysdb.MemberRanks(system.DownOutMemberFilter)
	if err != nil {
		return nil, err
	}

	ranksRequested := len(req.GetRanks()) > 0
	numRanksRequested := req.GetNumRanks() > 0
	if ranksRequested {
		// The caller supplied an explicit rank list. Deduplicate via RankSet
		// and verify every rank is a known system member (available or
		// excluded). Any requested rank that is excluded will be split out of
		// req.Ranks into req.UnavailableRanks below so that it becomes a
		// pool-map-only DOWNOUT entry rather than a VOS target create target.
		// Do NOT auto-add other system-wide excluded ranks in this path: an
		// explicit list is the caller's exact intent.
		requestedSet := ranklist.RankSetFromRanks(ranklist.RanksFromUint32(req.GetRanks()))
		knownSet := ranklist.RankSetFromRanks(availableRanks)
		for _, r := range downoutRanks {
			knownSet.Add(r)
		}
		if invalid := ranklist.CheckRankMembership(knownSet.Ranks(), requestedSet.Ranks()); len(invalid) > 0 {
			return nil, FaultPoolInvalidRanks(invalid)
		}
		req.Ranks = ranklist.RanksToUint32(requestedSet.Ranks())
	} else {
		// Otherwise, create the pool across the requested number of
		// available ranks in the system (if the request does not
		// specify a number of ranks, all are used).
		nAvailRanks := len(availableRanks)
		if numRanksRequested {
			nRanks := int(req.GetNumRanks())

			if nRanks > nAvailRanks {
				return nil, FaultPoolInvalidNumRanks(nRanks, nAvailRanks)
			}

			// TODO (DAOS-6263): Improve rank selection algorithm.
			// In the short term, we can just randomize the set of
			// available ranks in order to avoid always choosing the
			// first N ranks.
			rand.Seed(time.Now().UnixNano())
			rand.Shuffle(nAvailRanks, func(i, j int) {
				availableRanks[i], availableRanks[j] = availableRanks[j], availableRanks[i]
			})
			// With an explicit num_ranks, do NOT auto-admit excluded
			// ranks: the user asked for a specific size.
			req.Ranks = ranklist.RanksToUint32(availableRanks[:nRanks])
		} else {
			// Full-cluster default preserves the original target-create behavior:
			// only available ranks receive VOS/blob-store creation.
			req.Ranks = ranklist.RanksToUint32(availableRanks)
		}
		sort.Slice(req.Ranks, func(i, j int) bool { return req.Ranks[i] < req.Ranks[j] })
	}

	if len(req.GetRanks()) == 0 {
		return nil, errors.New("pool request contains zero target ranks")
	}

	// Populate req.UnavailableRanks with the ranks that must appear in the pool
	// map as DOWNOUT but must NOT receive VOS/blob-store creation.
	//
	//   * Explicit rank list: split any user-supplied excluded rank out of
	//     req.Ranks and merge it into any UnavailableRanks the caller already
	//     provided. We do not auto-add other system-wide excluded ranks
	//     because the explicit list is the caller's exact intent.
	//   * NumRanks: keep the shuffled subset of available ranks untouched; the
	//     caller asked for a specific active-rank count, so we do not admit
	//     any DOWNOUT ranks either.
	//   * Default (no rank spec, no client-populated UnavailableRanks): admit
	//     every excluded rank as DOWNOUT so the resulting pool map
	//     faithfully reflects the whole system. Well-behaved clients pre-
	//     populate both lists in this case, in which case we keep the caller's
	//     UnavailableRanks as-is.
	//
	// After this block both paths present a uniform semantic to the pool
	// service: req.Ranks == VOS target creators, req.UnavailableRanks ==
	// pool-map-only DOWNOUT entries, and the two lists are disjoint.
	unavailSet := ranklist.RankSetFromRanks(ranklist.RanksFromUint32(req.GetUnavailableRanks()))
	if ranksRequested {
		downoutSet := ranklist.RankSetFromRanks(downoutRanks)
		active := make([]uint32, 0, len(req.Ranks))
		for _, r := range req.Ranks {
			if downoutSet.Contains(ranklist.Rank(r)) {
				unavailSet.Add(ranklist.Rank(r))
			} else {
				active = append(active, r)
			}
		}
		req.Ranks = active
		req.UnavailableRanks = ranklist.RanksToUint32(unavailSet.Ranks())
		if len(req.Ranks) == 0 {
			return nil, errors.Errorf(
				"pool create requires at least one available target rank; "+
					"all of the requested ranks (%s) are excluded",
				unavailSet.String())
		}
	} else if !numRanksRequested && unavailSet.Count() == 0 {
		for _, r := range downoutRanks {
			unavailSet.Add(r)
		}
		req.UnavailableRanks = ranklist.RanksToUint32(unavailSet.Ranks())
	}

	// Clamp the maximum allowed svc replicas to the smaller of active target
	// ranks or MaxPoolServiceReps. req.Ranks and req.UnavailableRanks are
	// disjoint after the split block above, so the active count is simply
	// len(req.Ranks); poolCreateActiveRankCount handles any residual overlap
	// defensively.
	activeRankCount := poolCreateActiveRankCount(req)
	maxSvcReps := func(allRanks int) uint32 {
		if allRanks > MaxPoolServiceReps {
			return uint32(MaxPoolServiceReps)
		}
		return uint32(allRanks)
	}(activeRankCount)

	// If NumSvcReps is not specified, daos_engine will choose a value.
	if req.GetNumSvcReps() > maxSvcReps {
		return nil, FaultPoolInvalidServiceReps(maxSvcReps)
	}

	// Check if the requested redundancy factor can be met with the number of supplied fault domains.
	for _, prop := range req.GetProperties() {
		if prop.GetNumber() == uint32(daos.PoolPropertyRedunFac) {
			rdFac := int(prop.GetNumval())
			// Since the redundancy factor is specified, it is assumed that a fault‑domain level must exist.
			level, err := svc.membership.FaultDomainLevel()
			if err != nil {
				return nil, err
			}
			domainNr, err := svc.membership.DomainNr(level, req.Ranks...)
			if err != nil {
				return nil, err
			}
			if rdFac+1 > domainNr {
				return nil, FaultPoolTooFewFaultDomains(rdFac, domainNr)
			}
			break
		}
	}

	// IO engine needs the fault domain tree for placement purposes. Include
	// both active target ranks and pool-map-only DOWNOUT ranks.
	mapRankSet := ranklist.RankSetFromRanks(ranklist.RanksFromUint32(req.GetRanks()))
	for _, r := range req.GetUnavailableRanks() {
		mapRankSet.Add(ranklist.Rank(r))
	}
	req.FaultDomains, err = svc.membership.CompressedFaultDomainTree(
		ranklist.RanksToUint32(mapRankSet.Ranks())...)
	if err != nil {
		return nil, err
	}

	if err := svc.calculateCreateStorage(req); err != nil {
		return nil, err
	}

	ps = system.NewPoolService(poolUUID, req.TierBytes, req.MemRatio,
		ranklist.RanksFromUint32(req.GetRanks()))
	ps.PoolLabel = poolLabel
	if err := svc.sysdb.AddPoolService(ctx, ps); err != nil {
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
					req.Uuid, daos.Status(pdResp.GetStatus()))
			}
		}

		if cuErr == nil {
			svc.log.Debugf("removed pool service entry for %s in cleanup", req.Uuid)
			return
		}
	}()

	dResp, err := svc.harness.CallDrpc(ctx, daos.MethodPoolCreate, req)
	if err != nil {
		svc.log.Errorf("pool create dRPC call failed: %s", err)
		if err := svc.sysdb.RemovePoolService(ctx, ps.PoolUUID); err != nil {
			return nil, err
		}

		switch errors.Cause(err) {
		case errEngineNotReady:
			// If the pool create failed because there was no available instance
			// to service the request, signal to the client that it should try again.
			resp.Status = int32(daos.TryAgain)
			return resp, nil
		default:
			return nil, err
		}
	}

	if err := svc.unmarshalPB(dResp.Body, resp); err != nil {
		return nil, err
	}

	if resp.GetStatus() != 0 {
		if err := svc.sysdb.RemovePoolService(ctx, ps.PoolUUID); err != nil {
			return nil, err
		}

		return resp, nil
	}

	ps.Replicas = ranklist.RanksFromUint32(resp.GetSvcReps())
	ps.State = system.PoolServiceStateReady
	if err := svc.sysdb.UpdatePoolService(ctx, ps); err != nil {
		return nil, err
	}

	return resp, nil
}

func (svc *mgmtSvc) poolCreateAddSystemProps(req *mgmtpb.PoolCreateReq) error {
	poolSysProps := make(map[uint32]*daos.PoolProperty)
	for sp := range svc.systemProps.Iter() {
		pp, found := sp2pp(sp)
		if !found {
			continue
		}

		poolSysProps[pp.Number] = pp

		curVal, err := system.GetUserProperty(svc.sysdb, svc.systemProps, sp.Key.String())
		if err != nil {
			return err
		}

		if err := pp.SetValue(curVal); err != nil {
			return err
		}

		svc.log.Debugf("System Property '%+v' converted to Pool Property '%+v'", sp, pp)
	}

	if len(poolSysProps) == 0 {
		return nil
	}

	poolSetProps := make(map[uint32]*mgmtpb.PoolProperty)
	for _, p := range req.GetProperties() {
		poolSetProps[p.GetNumber()] = p
	}

	for k, p := range poolSysProps {
		if _, found := poolSetProps[k]; found {
			continue
		}
		pbProp := &mgmtpb.PoolProperty{
			Number: p.Number,
		}
		if nv, err := p.Value.GetNumber(); err == nil {
			pbProp.SetValueNumber(nv)
		} else {
			pbProp.SetValueString(p.Value.String())
		}
		req.Properties = append(req.Properties, pbProp)
	}

	return nil
}

// checkPools iterates over the list of pools in the system to check
// for any that are in an unexpected state. Pools not in the Ready
// state will be cleaned up and removed from the system.
func (svc *mgmtSvc) checkPools(parent context.Context, ignCreating bool, psList ...*system.PoolService) error {
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
		if ignCreating && ps.State == system.PoolServiceStateCreating {
			svc.log.Noticef("pool %s in %s state but cleanup skipped due to ignore", ps.PoolUUID, ps.State)
			continue
		}

		lock, err := svc.sysdb.TakePoolLock(parent, ps.PoolUUID)
		if err != nil {
			if fault.IsFaultCode(err, code.SystemPoolLocked) {
				svc.log.Noticef("pool %s not cleaned up due to err: %s", ps.PoolUUID, err)
				continue
			}
			return err
		}
		defer lock.Release()
		ctx := lock.InContext(parent)

		svc.log.Errorf("pool %s is in unexpected state %s", ps.PoolUUID, ps.State)

		// Change the pool state to Destroying in order to trigger
		// the cleanup mode of PoolDestroy(), which will cause the
		// destroy RPC to be sent to all ranks and then the service
		// will be removed from the system.
		if ps.State != system.PoolServiceStateDestroying {
			ps.State = system.PoolServiceStateDestroying
			if err := svc.sysdb.UpdatePoolService(ctx, ps); err != nil {
				return errors.Wrapf(err, "pool %s not updated", ps.PoolUUID)
			}
		}

		// Attempt to destroy the pool.
		dr := &mgmtpb.PoolDestroyReq{
			Sys:       svc.sysdb.SystemName(),
			Force:     true,
			Recursive: true,
			Id:        ps.PoolUUID.String(),
		}

		if _, err := svc.poolDestroyNoLeaderCheck(ctx, dr); err != nil {
			// Best effort cleanup. If the pool destroy fails here,
			// another leadership step-up should get it eventually.
			svc.log.Errorf("pool %s not destroyed: %s", ps.PoolUUID, err)
		}
	}

	return nil
}

func (svc *mgmtSvc) poolHasContainers(ctx context.Context, req *mgmtpb.PoolDestroyReq) (bool, error) {
	lcReq := &mgmtpb.ListContReq{}
	lcReq.Sys = req.Sys
	lcReq.Id = req.Id
	lcReq.SvcRanks = req.SvcRanks

	svc.log.Debugf("MgmtSvc.PoolDestroy issuing daos.MethodListContainers, req:%+v\n", lcReq)

	lcResp, err := svc.ListContainers(ctx, lcReq)
	if err != nil {
		svc.log.Debugf("svc.ListContainers failed\n")
		return false, err
	}

	dStatus := daos.Status(lcResp.GetStatus())
	if dStatus != daos.Success {
		return false, dStatus // daos.Status implements error
	}

	return len(lcResp.GetContainers()) > 0, nil
}

func (svc *mgmtSvc) poolEvictConnections(ctx context.Context, req *mgmtpb.PoolDestroyReq) (daos.Status, error) {
	evReq := &mgmtpb.PoolEvictReq{}
	evReq.Sys = req.Sys
	evReq.Id = req.Id
	evReq.SvcRanks = req.SvcRanks
	evReq.Destroy = true
	evReq.ForceDestroy = req.Force

	svc.log.Debugf("MgmtSvc.PoolDestroy issuing daos.MethodPoolEvict, req:%+v\n", evReq)

	evResp, err := svc.PoolEvict(ctx, evReq)
	if err != nil {
		svc.log.Errorf("svc.PoolEvict failed\n")
		return 0, err
	}

	svc.log.Debugf("MgmtSvc.PoolDestroy daos.MethodPoolEvict, resp:%+v\n", evResp)

	return daos.Status(evResp.GetStatus()), nil
}

// PoolDestroy implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolDestroy(parent context.Context, req *mgmtpb.PoolDestroyReq) (*mgmtpb.PoolDestroyResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}

	return svc.poolDestroyNoLeaderCheck(parent, req)
}

func (svc *mgmtSvc) poolDestroyNoLeaderCheck(parent context.Context, req *mgmtpb.PoolDestroyReq) (*mgmtpb.PoolDestroyResp, error) {
	poolUUID, err := svc.resolvePoolID(req.Id)
	if err != nil {
		return nil, err
	}

	lock, err := svc.sysdb.TakePoolLock(parent, poolUUID)
	if err != nil {
		return nil, err
	}
	defer lock.Release()
	ctx := lock.InContext(parent)

	ps, err := svc.sysdb.FindPoolServiceByUUID(poolUUID)
	if err != nil {
		return nil, err
	}
	req.SetUUID(poolUUID)
	req.SvcRanks = ranklist.RanksToUint32(ps.Replicas)

	resp := &mgmtpb.PoolDestroyResp{}

	if ps.State != system.PoolServiceStateDestroying {
		// If recursive flag is unset, refuse to destroy pool if resident containers exist.
		if !req.Recursive {
			hasContainers, err := svc.poolHasContainers(ctx, req)
			if err != nil {
				// Check if error is related to response status code.
				if dStatus, ok := err.(daos.Status); ok {
					svc.log.Errorf("ListContainers during pool destroy failed: %s", dStatus)
					resp.Status = int32(dStatus)
					return resp, nil
				}
				return nil, err
			}

			if hasContainers {
				return nil, FaultPoolHasContainers
			}
		}

		// Perform separate PoolEvict _before_ possible transition to destroying state.
		evStatus, err := svc.poolEvictConnections(ctx, req)
		if !req.Force && err != nil {
			return nil, err
		}

		// If the request is being forced, or the evict request did not fail
		// due to the pool being busy or service not up, then transition to the
		// destroying state and persist the update(s).
		if req.Force || (evStatus != daos.Busy && evStatus != daos.NoService) {
			ps.State = system.PoolServiceStateDestroying
			if err := svc.sysdb.UpdatePoolService(ctx, ps); err != nil {
				return nil, errors.Wrapf(err, "failed to update pool %s", poolUUID)
			}
		}

		if evStatus != daos.Success {
			svc.log.Errorf("PoolEvict during pool destroy failed: %s", evStatus)
			if !req.Force {
				resp.Status = int32(evStatus)
				return resp, nil
			}
		}
	}

	// Now on to the rest of the pool destroy, issue daos.MethodPoolDestroy.
	// Note that, here, we set req.SvcRanks to all ranks in the system, not
	// the PS replicas, not the up ranks in the pool. Doing such a "blind"
	// destroy avoids contacting the PS, who may have already been destroyed
	// by a previous pool destroy attempt or otherwise unavailable at this
	// point. Moreover, we will also clean up pool resources on ranks that
	// are now available but have previously been excluded from the pool.
	gm, err := svc.sysdb.GroupMap()
	if err != nil {
		return nil, err
	}
	allRanks := make([]uint32, 0, len(gm.RankEntries))
	for i := range gm.RankEntries {
		allRanks = append(allRanks, i.Uint32())
	}
	sort.Slice(allRanks, func(i, j int) bool { return allRanks[i] < allRanks[j] })
	req.SvcRanks = allRanks
	svc.log.Debugf("MgmtSvc.PoolDestroy issuing daos.MethodPoolDestroy: id=%s nSvcRanks=%d\n",
		req.Id, len(req.SvcRanks))
	dResp, err := svc.harness.CallDrpc(ctx, daos.MethodPoolDestroy, req)
	if err != nil {
		return nil, err
	}

	if err := svc.unmarshalPB(dResp.Body, resp); err != nil {
		return nil, err
	}

	ds := daos.Status(resp.Status)
	if ds == daos.Success {
		if err := svc.sysdb.RemovePoolService(ctx, poolUUID); err != nil {
			// In rare cases, there may be a race between pool cleanup handlers.
			// As we know the service entry existed when we started this handler,
			// if the attempt to remove it now fails because it doesn't exist,
			// then there's nothing else to do.
			if !system.IsPoolNotFound(err) {
				return nil, errors.Wrapf(err, "failed to remove pool %s", poolUUID)
			}
		}
	} else {
		svc.log.Errorf("PoolDestroy dRPC call failed: %s", ds)
	}

	return resp, nil
}

func (svc *mgmtSvc) evictPoolConnections(ctx context.Context, req *mgmtpb.PoolEvictReq) (*mgmtpb.PoolEvictResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}

	dResp, err := svc.makePoolServiceCall(ctx, daos.MethodPoolEvict, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolEvictResp{}
	if err := svc.unmarshalPB(dResp.Body, resp); err != nil {
		return nil, err
	}

	if resp.Count > 0 {
		svc.log.Infof("pool %s: evicted %d handle(s)", req.Id, resp.Count)
	}
	return resp, nil
}

// PoolEvict handles requests to evict pool handles. When a request contains
// multiple pool handles, it will be added to a batch request and processed
// with other handle eviction requests in order to reduce the number of dRPCs.
func (svc *mgmtSvc) PoolEvict(ctx context.Context, req *mgmtpb.PoolEvictReq) (*mgmtpb.PoolEvictResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}

	if len(req.Handles) == 0 {
		// If we're not evicting a set of handles, then we shouldn't bother with trying
		// to batch up the requests from multiple agents.
		return svc.evictPoolConnections(ctx, req)
	}

	msg, err := svc.submitBatchRequest(ctx, req)
	if err != nil {
		return nil, err
	}
	return msg.(*mgmtpb.PoolEvictResp), nil
}

// PoolExclude implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolExclude(ctx context.Context, req *mgmtpb.PoolExcludeReq) (*mgmtpb.PoolExcludeResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	if err := svc.checkRanksExist(req.Rank); err != nil {
		return nil, err
	}

	dResp, err := svc.makeLockedPoolServiceCall(ctx, daos.MethodPoolExclude, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolExcludeResp{}
	if err := svc.unmarshalPB(dResp.Body, resp); err != nil {
		return nil, err
	}

	return resp, nil
}

// PoolDrain implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolDrain(ctx context.Context, req *mgmtpb.PoolDrainReq) (*mgmtpb.PoolDrainResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	if err := svc.checkRanksExist(req.Rank); err != nil {
		return nil, err
	}

	dResp, err := svc.makeLockedPoolServiceCall(ctx, daos.MethodPoolDrain, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolDrainResp{}
	if err := svc.unmarshalPB(dResp.Body, resp); err != nil {
		return nil, err
	}

	return resp, nil
}

// PoolExtend implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolExtend(ctx context.Context, req *mgmtpb.PoolExtendReq) (*mgmtpb.PoolExtendResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}

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
	req.TierBytes = ps.Storage.PerRankTierStorage
	req.MemRatio = ps.Storage.MemRatio

	svc.log.Debugf("MgmtSvc.PoolExtend forwarding modified req:%+v\n", req)

	dResp, err := svc.makeLockedPoolServiceCall(ctx, daos.MethodPoolExtend, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolExtendResp{}
	if err := svc.unmarshalPB(dResp.Body, resp); err != nil {
		return nil, err
	}

	return resp, nil
}

// Return error if any requested rank is not in a valid state. Uses available rank filter under the
// hood so will only against ranks with joined/ready state.
func (svc *mgmtSvc) checkRanksExist(rl ...uint32) error {
	rs := ranklist.RankSetFromRanks(ranklist.RanksFromUint32(rl))
	_, miss, err := svc.membership.CheckRanks(rs.String())
	if err != nil {
		return err
	}
	if miss.Count() != 0 {
		return FaultPoolInvalidRanks(miss.Ranks())
	}

	return nil
}

// PoolReintegrate implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolReintegrate(ctx context.Context, req *mgmtpb.PoolReintReq) (*mgmtpb.PoolReintResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	if err := svc.checkRanksExist(req.Rank); err != nil {
		return nil, err
	}

	// Look up the pool service record to find the storage allocations used at creation.
	ps, err := svc.getPoolService(req.GetId())
	if err != nil {
		return nil, err
	}
	req.TierBytes = ps.Storage.PerRankTierStorage
	req.MemRatio = ps.Storage.MemRatio

	dResp, err := svc.makeLockedPoolServiceCall(ctx, daos.MethodPoolReintegrate, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolReintResp{}
	if err := svc.unmarshalPB(dResp.Body, resp); err != nil {
		return nil, err
	}

	return resp, nil
}

// PoolQuery forwards a pool query request to the I/O Engine.
func (svc *mgmtSvc) PoolQuery(ctx context.Context, req *mgmtpb.PoolQueryReq) (*mgmtpb.PoolQueryResp, error) {
	if err := svc.checkReplicaRequest(req); err != nil {
		return nil, err
	}

	if req.QueryMask == 0 {
		req.QueryMask = uint64(daos.DefaultPoolQueryMask)
	}

	dResp, err := svc.makePoolServiceCall(ctx, daos.MethodPoolQuery, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolQueryResp{}
	if err := svc.unmarshalPB(dResp.Body, resp); err != nil {
		return nil, err
	}

	// Preserve compatibility with pre-2.6 callers.
	resp.Leader = resp.SvcLdr

	// Retrieve system self-heal property. Assume default value where all flags are set if
	// property isn't present.
	resp.SysSelfHealPolicy = daos.DefaultSysSelfHealFlagsStr
	if req.QueryMask&C.DPI_SELF_HEAL_POLICY != 0 {
		if selfHeal, err := svc.getSysSelfHeal(); system.IsErrSystemAttrNotFound(err) {
			svc.log.Debugf(err.Error())
		} else if err != nil {
			return nil, err
		} else {
			svc.log.Debugf("system self-heal: %s", selfHeal)
			resp.SysSelfHealPolicy = selfHeal
		}
	}

	return resp, nil
}

// PoolQueryTarget forwards a pool query targets request to the I/O Engine.
func (svc *mgmtSvc) PoolQueryTarget(ctx context.Context, req *mgmtpb.PoolQueryTargetReq) (*mgmtpb.PoolQueryTargetResp, error) {
	if err := svc.checkReplicaRequest(req); err != nil {
		return nil, err
	}

	dResp, err := svc.makePoolServiceCall(ctx, daos.MethodPoolQueryTarget, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolQueryTargetResp{}
	if err := svc.unmarshalPB(dResp.Body, resp); err != nil {
		return nil, err
	}

	return resp, nil
}

// poolServiceSimple is a helper that implements forwarding a pool service request and returns
// DaosResp.
func (svc *mgmtSvc) poolServiceSimple(ctx context.Context, req poolServiceReq, meth daos.MgmtMethod) (*mgmtpb.DaosResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}

	dResp, err := svc.makeLockedPoolServiceCall(ctx, meth, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.DaosResp{}
	if err := svc.unmarshalPB(dResp.Body, resp); err != nil {
		return nil, err
	}

	return resp, nil
}

// PoolUpgrade forwards a pool upgrade request to the I/O Engine.
func (svc *mgmtSvc) PoolUpgrade(ctx context.Context, req *mgmtpb.PoolUpgradeReq) (*mgmtpb.DaosResp, error) {
	return svc.poolServiceSimple(ctx, req, daos.MethodPoolUpgrade)
}

func (svc *mgmtSvc) updatePoolLabel(ctx context.Context, sys string, uuid uuid.UUID, prop *mgmtpb.PoolProperty) error {
	if prop.GetNumber() != daos.PoolPropertyLabel {
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

	var dResp *drpc.Response
	dResp, err = svc.makePoolServiceCall(ctx, daos.MethodPoolSetProp, req)
	if err != nil {
		return err
	}

	resp := new(mgmtpb.PoolSetPropResp)
	if err := svc.unmarshalPB(dResp.Body, resp); err != nil {
		return err
	}

	if resp.GetStatus() != 0 {
		return errors.Errorf("label update failed: %s", drpc.Status(resp.Status))
	}

	// Persist the label update in the MS DB if the
	// dRPC call succeeded.
	ps.PoolLabel = label
	return svc.sysdb.UpdatePoolService(ctx, ps)
}

// PoolSetProp forwards a request to the I/O Engine to set pool properties.
func (svc *mgmtSvc) PoolSetProp(parent context.Context, req *mgmtpb.PoolSetPropReq) (*mgmtpb.PoolSetPropResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}

	poolUUID, err := svc.resolvePoolID(req.GetId())
	if err != nil {
		return nil, err
	}

	lock, err := svc.sysdb.TakePoolLock(parent, poolUUID)
	if err != nil {
		return nil, err
	}
	defer lock.Release()
	ctx := lock.InContext(parent)

	if len(req.GetProperties()) == 0 {
		return nil, errors.New("PoolSetProp() request with 0 properties")
	}

	miscProps := make([]*mgmtpb.PoolProperty, 0, len(req.GetProperties()))
	for _, prop := range req.GetProperties() {
		// Label is a special case, in that we need to ensure that it's unique
		// and also to update the pool service entry. Handle it first and separately
		// so that if it fails, none of the other props are changed.
		if prop.GetNumber() == daos.PoolPropertyLabel {
			if err := svc.updatePoolLabel(ctx, req.GetSys(), poolUUID, prop); err != nil {
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

	var dResp *drpc.Response
	dResp, err = svc.makePoolServiceCall(ctx, daos.MethodPoolSetProp, req)
	if err != nil {
		return nil, err
	}

	if err := svc.unmarshalPB(dResp.Body, resp); err != nil {
		return nil, err
	}

	return resp, nil
}

// PoolGetProp forwards a request to the I/O Engine to get pool properties.
func (svc *mgmtSvc) PoolGetProp(ctx context.Context, req *mgmtpb.PoolGetPropReq) (*mgmtpb.PoolGetPropResp, error) {
	if err := svc.checkReplicaRequest(req); err != nil {
		return nil, err
	}

	// The request must contain a list of expected properties. We don't want
	// to just let the engine return all properties because not all properties
	// are valid to retrieve this way (e.g. ACL, etc).
	if len(req.GetProperties()) == 0 {
		return nil, errors.Errorf("PoolGetProp() request with 0 properties")
	}

	dResp, err := svc.makePoolServiceCall(ctx, daos.MethodPoolGetProp, req)
	if err != nil {
		return nil, err
	}

	resp := new(mgmtpb.PoolGetPropResp)
	if err := svc.unmarshalPB(dResp.Body, resp); err != nil {
		return nil, err
	}

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

	dResp, err := svc.makePoolServiceCall(ctx, daos.MethodPoolGetACL, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ACLResp{}
	if err := svc.unmarshalPB(dResp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "PoolGetACL")
	}

	return resp, nil
}

// PoolOverwriteACL forwards a request to the I/O Engine to overwrite a pool's Access Control List
func (svc *mgmtSvc) PoolOverwriteACL(ctx context.Context, req *mgmtpb.ModifyACLReq) (*mgmtpb.ACLResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}

	dResp, err := svc.makeLockedPoolServiceCall(ctx, daos.MethodPoolOverwriteACL, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ACLResp{}
	if err := svc.unmarshalPB(dResp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "PoolOverwriteACL")
	}

	return resp, nil
}

// PoolUpdateACL forwards a request to the I/O Engine to add or update entries in
// a pool's Access Control List
func (svc *mgmtSvc) PoolUpdateACL(ctx context.Context, req *mgmtpb.ModifyACLReq) (*mgmtpb.ACLResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}

	dResp, err := svc.makeLockedPoolServiceCall(ctx, daos.MethodPoolUpdateACL, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ACLResp{}
	if err := svc.unmarshalPB(dResp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "PoolUpdateACL")
	}

	return resp, nil
}

// PoolDeleteACL forwards a request to the I/O Engine to delete an entry from a
// pool's Access Control List.
func (svc *mgmtSvc) PoolDeleteACL(ctx context.Context, req *mgmtpb.DeleteACLReq) (*mgmtpb.ACLResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}

	dResp, err := svc.makeLockedPoolServiceCall(ctx, daos.MethodPoolDeleteACL, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ACLResp{}
	if err := svc.unmarshalPB(dResp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "PoolDeleteACL")
	}

	return resp, nil
}

// ListPools returns a set of all pools in the system.
func (svc *mgmtSvc) ListPools(ctx context.Context, req *mgmtpb.ListPoolsReq) (*mgmtpb.ListPoolsResp, error) {
	if err := svc.checkReplicaRequest(wrapCheckerReq(req)); err != nil {
		return nil, err
	}

	psList, err := svc.sysdb.PoolServiceList(true)
	if err != nil {
		return nil, err
	}

	resp := new(mgmtpb.ListPoolsResp)
	for _, ps := range psList {
		resp.Pools = append(resp.Pools, &mgmtpb.ListPoolsResp_Pool{
			Uuid:    ps.PoolUUID.String(),
			Label:   ps.PoolLabel,
			SvcReps: ranklist.RanksToUint32(ps.Replicas),
			State:   ps.State.String(),
		})
	}

	v, err := svc.sysdb.DataVersion()
	if err != nil {
		return nil, err
	}
	resp.DataVersion = v

	return resp, nil
}

// PoolRebuildStart forwards a pool interactive rebuild start request to the I/O Engine.
func (svc *mgmtSvc) PoolRebuildStart(ctx context.Context, req *mgmtpb.PoolRebuildStartReq) (*mgmtpb.DaosResp, error) {
	return svc.poolServiceSimple(ctx, req, daos.MethodPoolRebuildStart)
}

// PoolRebuildStop forwards a pool interactive rebuild stop request to the I/O Engine.
func (svc *mgmtSvc) PoolRebuildStop(ctx context.Context, req *mgmtpb.PoolRebuildStopReq) (*mgmtpb.DaosResp, error) {
	return svc.poolServiceSimple(ctx, req, daos.MethodPoolRebuildStop)
}

// PoolSelfHealEval forwards a pool self-heal evaluate request to the I/O Engine.
func (svc *mgmtSvc) PoolSelfHealEval(ctx context.Context, req *mgmtpb.PoolSelfHealEvalReq) (*mgmtpb.DaosResp, error) {
	return svc.poolServiceSimple(ctx, req, daos.MethodPoolSelfHealEval)
}
