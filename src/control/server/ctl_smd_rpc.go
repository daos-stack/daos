//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"sort"
	"strings"
	"time"

	uuid "github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
)

// Set as variables so can be overwritten during unit testing.
var (
	baseDevReplaceBackoff      = 250 * time.Millisecond
	maxDevReplaceBackoffFactor = 7 // 8s
	maxDevReplaceRetries       = 20
)

func queryRank(reqRank uint32, engineRank ranklist.Rank) bool {
	rr := ranklist.Rank(reqRank)
	if rr.Equals(ranklist.NilRank) {
		return true
	}
	return rr.Equals(engineRank)
}

func (svc *ControlService) querySmdDevices(ctx context.Context, req *ctlpb.SmdQueryReq, resp *ctlpb.SmdQueryResp) error {
	if req == nil {
		return errors.New("nil request")
	}

	for _, ei := range svc.harness.Instances() {
		if !ei.IsReady() {
			svc.log.Debugf("skipping not-ready instance")
			continue
		}

		engineRank, err := ei.GetRank()
		if err != nil {
			return err
		}
		if !queryRank(req.GetRank(), engineRank) {
			svc.log.Debugf("skipping rank %d not specified in request", engineRank)
			continue
		}

		rResp, err := smdQueryEngine(ctx, ei, req)
		if err != nil {
			return err
		}
		resp.Ranks = append(resp.Ranks, rResp)
	}

	return nil
}

func (svc *ControlService) querySmdPools(ctx context.Context, req *ctlpb.SmdQueryReq, resp *ctlpb.SmdQueryResp) error {
	for _, ei := range svc.harness.Instances() {
		if !ei.IsReady() {
			svc.log.Debugf("skipping not-ready instance")
			continue
		}

		engineRank, err := ei.GetRank()
		if err != nil {
			return err
		}
		if !queryRank(req.GetRank(), engineRank) {
			svc.log.Debugf("skipping rank %d not specified in request", engineRank)
			continue
		}

		rResp := new(ctlpb.SmdQueryResp_RankResp)
		rResp.Rank = engineRank.Uint32()

		dresp, err := ei.CallDrpc(ctx, drpc.MethodSmdPools, new(ctlpb.SmdPoolReq))
		if err != nil {
			return err
		}

		rankDevResp := new(ctlpb.SmdPoolResp)
		if err = proto.Unmarshal(dresp.Body, rankDevResp); err != nil {
			return errors.Wrap(err, "unmarshal SmdListPools response")
		}

		if rankDevResp.Status != 0 {
			return errors.Wrapf(daos.Status(rankDevResp.Status),
				"rank %d ListPools failed", engineRank)
		}

		if err := convert.Types(rankDevResp.Pools, &rResp.Pools); err != nil {
			return errors.Wrap(err, "failed to convert pool list")
		}
		resp.Ranks = append(resp.Ranks, rResp)

		if req.Uuid != "" {
			found := false
			for _, pool := range rResp.Pools {
				if pool.Uuid == req.Uuid {
					rResp.Pools = []*ctlpb.SmdQueryResp_Pool{pool}
					found = true
					break
				}
			}
			if !found {
				rResp.Pools = nil
			}
		}
	}

	return nil
}

// SmdQuery implements the method defined for the Management Service.
//
// Query SMD info for pools or devices.
func (svc *ControlService) SmdQuery(ctx context.Context, req *ctlpb.SmdQueryReq) (*ctlpb.SmdQueryResp, error) {
	if !svc.harness.isStarted() {
		return nil, FaultHarnessNotStarted
	}
	if len(svc.harness.readyRanks()) == 0 {
		return nil, FaultDataPlaneNotStarted
	}

	if req.Uuid != "" && (!req.OmitDevices && !req.OmitPools) {
		return nil, errors.New("UUID is ambiguous when querying both pools and devices")
	}

	resp := new(ctlpb.SmdQueryResp)
	if !req.OmitDevices {
		if err := svc.querySmdDevices(ctx, req, resp); err != nil {
			return nil, err
		}
	}
	if !req.OmitPools {
		if err := svc.querySmdPools(ctx, req, resp); err != nil {
			return nil, err
		}
	}

	return resp, nil
}

type idMap map[string]bool

func (im idMap) Keys() (keys []string) {
	for k := range im {
		keys = append(keys, k)
	}
	return
}

// Split IDs in comma separated string and assign each token to relevant return list.
func extractReqIDs(log logging.Logger, ids string, addrs idMap, uuids idMap) error {
	tokens := strings.Split(ids, ",")

	for _, token := range tokens {
		if addr, e := hardware.NewPCIAddress(token); e == nil && addr.IsVMDBackingAddress() {
			addrs[addr.String()] = true
			continue
		}

		if uuid, e := uuid.Parse(token); e == nil {
			uuids[uuid.String()] = true
			continue
		}

		return errors.Errorf("req id entry %q is neither a valid vmd backing device pci "+
			"address or uuid", token)
	}

	return nil
}

// Union type containing either traddr or uuid.
type devID struct {
	trAddr string
	uuid   string
}

func (id *devID) String() string {
	if id.trAddr != "" {
		return id.trAddr
	}
	return id.uuid
}

type devIDMap map[string]devID

func (dim devIDMap) getFirst() *devID {
	if len(dim) == 0 {
		return nil
	}

	var keys []string
	for key := range dim {
		keys = append(keys, key)
	}
	sort.Strings(keys)

	d := dim[keys[0]]
	return &d
}

type engineDevMap map[Engine]devIDMap

func (edm engineDevMap) add(e Engine, id devID) {
	if _, exists := edm[e]; !exists {
		edm[e] = make(devIDMap)
	}
	if _, exists := edm[e][id.String()]; !exists {
		edm[e][id.String()] = id
	}
}

// Map requested device IDs provided in comma-separated string to the engine that controls the given
// device. Device can be identified either by UUID or transport (PCI) address.
func (svc *ControlService) mapIDsToEngine(ctx context.Context, ids string, useTrAddr bool) (engineDevMap, error) {
	trAddrs := make(idMap)
	devUUIDs := make(idMap)
	matchAll := false

	if ids == "" {
		// Selecting all is not supported unless using transport addresses.
		if !useTrAddr {
			return nil, errors.New("empty id string")
		}
		matchAll = true
	} else {
		// Extract transport addresses and device UUIDs from IDs string.
		if err := extractReqIDs(svc.log, ids, trAddrs, devUUIDs); err != nil {
			return nil, err
		}
	}

	req := &ctlpb.SmdQueryReq{Rank: uint32(ranklist.NilRank)}
	resp := new(ctlpb.SmdQueryResp)
	if err := svc.querySmdDevices(ctx, req, resp); err != nil {
		return nil, err
	}

	edm := make(engineDevMap)

	for _, rr := range resp.Ranks {
		engines, err := svc.harness.FilterInstancesByRankSet(fmt.Sprintf("%d", rr.Rank))
		if err != nil {
			return nil, err
		}
		if len(engines) == 0 {
			return nil, errors.Errorf("failed to retrieve instance for rank %d",
				rr.Rank)
		}
		engine := engines[0]
		for _, dev := range rr.Devices {
			if dev == nil {
				return nil, errors.New("nil device in smd query resp")
			}
			if dev.Ctrlr.PciAddr == "" {
				svc.log.Errorf("No transport address associated with device %s",
					dev.Uuid)
			}

			matchUUID := dev.Uuid != "" && devUUIDs[dev.Uuid]

			// Where possible specify the TrAddr over UUID as there may be multiple
			// UUIDs mapping to the same TrAddr.
			if useTrAddr && dev.Ctrlr.PciAddr != "" {
				if matchAll || matchUUID || trAddrs[dev.Ctrlr.PciAddr] {
					// If UUID matches, add by TrAddr rather than UUID which
					// should avoid duplicate UUID entries for the same TrAddr.
					edm.add(engine, devID{trAddr: dev.Ctrlr.PciAddr})
					delete(trAddrs, dev.Ctrlr.PciAddr)
					delete(devUUIDs, dev.Uuid)
					continue
				}
			}

			if matchUUID {
				// Only add UUID entry if TrAddr is not available for a device.
				edm.add(engine, devID{uuid: dev.Uuid})
				delete(devUUIDs, dev.Uuid)
			}
		}
	}

	// Check all input IDs have been matched.
	missingKeys := append(devUUIDs.Keys(), trAddrs.Keys()...)
	if len(missingKeys) > 0 {
		return nil, errors.Errorf("ids requested but not found: %v", missingKeys)
	}

	return edm, nil
}

func sendManageReq(c context.Context, e Engine, m drpc.Method, b proto.Message) (*ctlpb.SmdManageResp_Result, error) {
	if !e.IsReady() {
		return &ctlpb.SmdManageResp_Result{
			Status: daos.Unreachable.Int32(),
		}, nil
	}

	dResp, err := e.CallDrpc(c, m, b)
	if err != nil {
		return nil, errors.Wrap(err, "call drpc")
	}

	mResp := new(ctlpb.DevManageResp)
	if err = proto.Unmarshal(dResp.Body, mResp); err != nil {
		return nil, errors.Wrapf(err, "unmarshal %T response", mResp)
	}

	return &ctlpb.SmdManageResp_Result{
		Status: mResp.Status, Device: mResp.Device,
	}, nil
}

func addManageRespIDOnFail(log logging.Logger, res *ctlpb.SmdManageResp_Result, dev *devID) {
	if res == nil || dev == nil || res.Status == 0 {
		return
	}

	log.Errorf("drpc returned status %q on dev %+v", daos.Status(res.Status), dev)
	if res.Device == nil {
		// Populate id so failure can be mapped to a device.
		res.Device = &ctlpb.SmdDevice{
			Ctrlr: &ctlpb.NvmeController{
				PciAddr: dev.trAddr,
			},
			Uuid: dev.uuid,
		}
	}
}

// Retry dev-replace requests as state propagation may take some time after set-faulty call has
// been made to manually trigger a faulty device state.
func replaceDevRetryBusy(ctx context.Context, log logging.Logger, e Engine, req proto.Message) (res *ctlpb.SmdManageResp_Result, err error) {
	for try := 0; try < maxDevReplaceRetries; try++ {
		res, err = sendManageReq(ctx, e, drpc.MethodReplaceStorage, req)
		if err != nil {
			return
		}
		if daos.Status(res.Status) != daos.Busy || try == maxDevReplaceRetries-1 {
			break
		}

		backoff := common.ExpBackoff(baseDevReplaceBackoff, uint64(try),
			uint64(maxDevReplaceBackoffFactor))
		log.Debugf("retrying dev-replace drpc request after %s", backoff)

		select {
		case <-ctx.Done():
			err = ctx.Err()
			return
		case <-time.After(backoff):
		}
	}

	return
}

func (svc *ControlService) singleDevSmdManage(ctx context.Context, req *ctlpb.SmdManageReq, id string) ([]*ctlpb.SmdManageResp_RankResp, error) {
	// Evaluate which engine(s) to send requests to.
	engineDevMap, err := svc.mapIDsToEngine(ctx, id, false)
	if err != nil {
		return nil, errors.Wrap(err, "mapping device identifiers to engine")
	}
	if len(engineDevMap) != 1 {
		return nil, errors.Errorf("%T request expects only one engine in device map",
			req.Op)
	}

	var engine Engine
	for engine, _ = range engineDevMap {
		break
	}
	devs := engineDevMap[engine]
	if len(devs) != 1 {
		return nil, errors.Errorf("%T request expects only one device in map", req.Op)
	}

	rank, err := engine.GetRank()
	if err != nil {
		return nil, errors.Wrap(err, "retrieving engine rank")
	}

	var devRes *ctlpb.SmdManageResp_Result
	msg := fmt.Sprintf("CtlSvc.SmdManage dispatch, rank %d", rank)

	// Extract request from oneof field and execute dRPC.
	switch req.Op.(type) {
	case *ctlpb.SmdManageReq_Replace:
		dReq := req.GetReplace()
		msg := fmt.Sprintf("%s dev-replace", msg)
		devRes, err = replaceDevRetryBusy(ctx, svc.log, engine, dReq)
		svc.log.Tracef("%s: req %+v, resp %+v", msg, dReq, devRes)
	case *ctlpb.SmdManageReq_Faulty:
		dReq := req.GetFaulty()
		msg := fmt.Sprintf("%s set-faulty", msg)
		devRes, err = sendManageReq(ctx, engine, drpc.MethodSetFaultyState, dReq)
		svc.log.Tracef("%s: req %+v, resp %+v", msg, dReq, devRes)
	default:
		return nil, errors.Errorf("unexpected smd manage request type, want "+
			"SmdManageReq_(Replace|Faulty) got %T", req.Op)
	}

	if err != nil {
		return nil, errors.Wrap(err, msg)
	}

	return []*ctlpb.SmdManageResp_RankResp{
		{
			Rank: rank.Uint32(), Results: []*ctlpb.SmdManageResp_Result{
				{Status: devRes.Status},
			},
		},
	}, nil
}

func (svc *ControlService) multiDevSmdManage(ctx context.Context, req *ctlpb.SmdManageReq, ids string) ([]*ctlpb.SmdManageResp_RankResp, error) {
	// Only LED manage requests currently support operation over multiple devices.
	if _, ok := req.Op.(*ctlpb.SmdManageReq_Led); !ok {
		return nil, errors.Errorf("unexpected smd manage request type, want "+
			"SmdManageReq_Led got %T", req.Op)
	}

	// Evaluate which engine(s) to send requests to. The last bool param indicates that the
	// Device-UUID can be replaced with its parent NVMe controller address during processing of
	// the request.
	engineDevMap, err := svc.mapIDsToEngine(ctx, ids, true)
	if err != nil {
		return nil, errors.Wrap(err, "mapping device identifiers to engine")
	}
	if len(engineDevMap) == 0 {
		return nil, errors.Errorf("%T request expects at least one engine in device map",
			req.Op)
	}

	var rankResps []*ctlpb.SmdManageResp_RankResp
	for engine, devs := range engineDevMap {
		devResults := []*ctlpb.SmdManageResp_Result{}

		if len(devs) == 0 {
			return nil, errors.Errorf("%T request expects at least one device in map",
				req.Op)
		}

		rank, err := engine.GetRank()
		if err != nil {
			return nil, errors.Wrap(err, "retrieving engine rank")
		}

		msg := fmt.Sprintf("CtlSvc.SmdManage dispatch, rank %d led-manage", rank)

		// Multiple addresses are supported in LED request.
		for _, dev := range devs {
			dReq := req.GetLed()
			// ID should by now have been resolved to a transport (PCI) address.
			if dev.trAddr == "" {
				return nil, errors.Errorf("device UUID %s not resolved to a PCI "+
					"address", dev.uuid)
			}
			dReq.Ids = dev.trAddr
			devRes, err := sendManageReq(ctx, engine, drpc.MethodLedManage, dReq)
			if err != nil {
				return nil, errors.Wrap(err, msg)
			}
			addManageRespIDOnFail(svc.log, devRes, &dev)
			svc.log.Tracef("%s: req %+v, resp %+v", msg, dReq, devRes)
			devResults = append(devResults, devRes)
		}

		rankResps = append(rankResps, &ctlpb.SmdManageResp_RankResp{
			Rank: rank.Uint32(), Results: devResults,
		})
	}

	sort.Slice(rankResps, func(i, j int) bool {
		return rankResps[i].Rank < rankResps[j].Rank
	})

	return rankResps, nil
}

// SmdManage implements the method defined for the Management Service.
//
// Manage SMD devices.
func (svc *ControlService) SmdManage(ctx context.Context, req *ctlpb.SmdManageReq) (*ctlpb.SmdManageResp, error) {
	if !svc.harness.isStarted() {
		return nil, FaultHarnessNotStarted
	}
	if len(svc.harness.readyRanks()) == 0 {
		return nil, FaultDataPlaneNotStarted
	}

	var rankResps []*ctlpb.SmdManageResp_RankResp
	var err error

	switch req.Op.(type) {
	case *ctlpb.SmdManageReq_Replace:
		rankResps, err = svc.singleDevSmdManage(ctx, req, req.GetReplace().OldDevUuid)
	case *ctlpb.SmdManageReq_Faulty:
		rankResps, err = svc.singleDevSmdManage(ctx, req, req.GetFaulty().Uuid)
	case *ctlpb.SmdManageReq_Led:
		rankResps, err = svc.multiDevSmdManage(ctx, req, req.GetLed().Ids)
	default:
		return nil, errors.Errorf("Unrecognized operation in SmdManageReq: %+v", req.Op)
	}

	if err != nil {
		return nil, err
	}

	return &ctlpb.SmdManageResp{Ranks: rankResps}, nil
}
