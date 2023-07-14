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

	uuid "github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
)

func queryRank(reqRank uint32, engineRank ranklist.Rank) bool {
	rr := ranklist.Rank(reqRank)
	if rr.Equals(ranklist.NilRank) {
		return true
	}
	return rr.Equals(engineRank)
}

func (svc *ControlService) querySmdDevices(ctx context.Context, req *ctlpb.SmdQueryReq, resp *ctlpb.SmdQueryResp) error {
	for _, ei := range svc.harness.Instances() {
		if !ei.IsReady() {
			svc.log.Debugf("skipping not-ready instance %d", ei.Index())
			continue
		}

		engineRank, err := ei.GetRank()
		if err != nil {
			return err
		}
		if !queryRank(req.GetRank(), engineRank) {
			continue
		}

		rResp := new(ctlpb.SmdQueryResp_RankResp)
		rResp.Rank = engineRank.Uint32()

		listDevsResp, err := ei.ListSmdDevices(ctx, new(ctlpb.SmdDevReq))
		if err != nil {
			return errors.Wrapf(err, "rank %d", engineRank)
		}

		if len(listDevsResp.Devices) == 0 {
			rResp.Devices = nil
			resp.Ranks = append(resp.Ranks, rResp)
			continue
		}

		// For each SmdDevice returned in list devs response, append a SmdDeviceWithHealth.
		for _, sd := range listDevsResp.Devices {
			rResp.Devices = append(rResp.Devices, &ctlpb.SmdQueryResp_SmdDeviceWithHealth{
				Details: sd,
			})
		}
		resp.Ranks = append(resp.Ranks, rResp)

		if req.Uuid != "" {
			found := false
			for _, dev := range rResp.Devices {
				if dev.Details.Uuid == req.Uuid {
					rResp.Devices = []*ctlpb.SmdQueryResp_SmdDeviceWithHealth{dev}
					found = true
					break
				}
			}
			if !found {
				rResp.Devices = nil
			}
		}

		for _, dev := range rResp.Devices {
			state := dev.Details.DevState

			// skip health query if the device is not in a normal or faulty state
			if req.IncludeBioHealth {
				if state != ctlpb.NvmeDevState_NEW {
					health, err := ei.GetBioHealth(ctx, &ctlpb.BioHealthReq{
						DevUuid: dev.Details.Uuid,
					})
					if err != nil {
						return errors.Wrapf(err, "device %q, state %q",
							dev, state)
					}
					dev.Health = health
					continue
				}
				svc.log.Debugf("skip fetching health stats on device %q in NEW state",
					dev, state)
			}
		}
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
func extractReqIDs(log logging.Logger, ids string) (idMap, idMap, error) {
	if ids == "" {
		return nil, nil, errors.New("empty id string")
	}

	tokens := strings.Split(ids, ",")

	addrs := make(idMap)
	uuids := make(idMap)

	for _, token := range tokens {
		if addr, err := hardware.NewPCIAddress(token); err == nil && addr.IsVMDBackingAddress() {
			addrs[addr.String()] = true
			continue
		}

		if uuid, err := uuid.Parse(token); err == nil {
			uuids[uuid.String()] = true
			continue
		}

		return nil, nil, errors.Errorf("req id entry %q is neither a valid vmd backing "+
			"device pci address or uuid", token)
	}

	return addrs, uuids, nil
}

type devID struct {
	trAddr string
	uuid   string
}

type engineDevMap map[Engine][]devID

// Map requested device IDs provided in comma-separated string to the engine that controls the given
// device. Device can be identified either by UUID or transport (PCI) address.
func (svc *ControlService) mapIDsToEngine(ctx context.Context, ids string, useTrAddr bool) (engineDevMap, error) {
	// Extract transport addresses and device UUIDs from IDs string.
	trAddrs, devUUIDs, err := extractReqIDs(svc.log, ids)
	if err != nil {
		return nil, err
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
			return nil, errors.Errorf("failed to retrieve instance for rank %d", rr.Rank)
		}
		engine := engines[0]
		for _, dev := range rr.Devices {
			if dev == nil {
				return nil, errors.New("nil device in smd query resp")
			}
			dds := dev.Details
			if dds == nil {
				return nil, errors.New("device with nil details in smd query resp")
			}

			uuidMatch := dds.Uuid != "" && devUUIDs[dds.Uuid]
			// Where possible specify the TrAddr over UUID as there may be multiple
			// UUIDs mapping to the same TrAddr.
			if useTrAddr && dds.TrAddr != "" {
				if trAddrs[dds.TrAddr] || uuidMatch {
					// If UUID matches, add by TrAddr rather than UUID which
					// should avoid duplicate UUID entries for the same TrAddr.
					edm[engine] = append(edm[engine], devID{trAddr: dds.TrAddr})
					delete(trAddrs, dds.TrAddr)
					delete(devUUIDs, dds.Uuid)
					continue
				}
			}

			if uuidMatch {
				// Only add UUID entry if TrAddr is not available for a device.
				edm[engine] = append(edm[engine], devID{uuid: dds.Uuid})
				delete(devUUIDs, dds.Uuid)
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

func addManageRespIDOnFail(log logging.Logger, res *ctlpb.SmdManageResp_Result, dev devID) {
	if res.Status != 0 {
		log.Errorf("drpc returned status %q on dev %+v", daos.Status(res.Status), dev)
		if res.Device == nil {
			// Populate id so failure can be mapped to a device.
			res.Device = &ctlpb.SmdDevice{
				TrAddr: dev.trAddr, Uuid: dev.uuid,
			}
		}
	}
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

	// Flag indicates whether Device-UUID can be replaced with its parent NVMe controller address.
	var useTrAddrInReq bool
	var ids string

	switch req.Op.(type) {
	case *ctlpb.SmdManageReq_Replace:
		ids = req.GetReplace().OldDevUuid
	case *ctlpb.SmdManageReq_Faulty:
		ids = req.GetFaulty().Uuid
	case *ctlpb.SmdManageReq_Led:
		useTrAddrInReq = true
		ids = req.GetLed().Ids
	default:
		return nil, errors.Errorf("Unrecognized operation in SmdManageReq: %+v", req.Op)
	}

	// Evaluate which engine(s) to send requests to.
	engineDevMap, err := svc.mapIDsToEngine(ctx, ids, useTrAddrInReq)
	if err != nil {
		return nil, errors.Wrap(err, "mapping device identifiers to engine")
	}

	rankResps := []*ctlpb.SmdManageResp_RankResp{}

	for engine, devs := range engineDevMap {
		devResults := []*ctlpb.SmdManageResp_Result{}

		rank, err := engine.GetRank()
		if err != nil {
			return nil, errors.Wrap(err, "retrieving engine rank")
		}

		msg := fmt.Sprintf("CtlSvc.SmdManage dispatch, rank %d: %%s req:%%+v\n", rank)

		// Extract request from oneof field and execute dRPC.
		switch req.Op.(type) {
		case *ctlpb.SmdManageReq_Replace:
			if len(devs) != 1 {
				return nil, errors.New("replace request expects only one device ID")
			}
			dReq := req.GetReplace()
			svc.log.Debugf(msg, "dev-replace", dReq)
			devRes, err := sendManageReq(ctx, engine, drpc.MethodReplaceStorage, dReq)
			if err != nil {
				return nil, err
			}
			addManageRespIDOnFail(svc.log, devRes, devs[0])
			devResults = append(devResults, devRes)
		case *ctlpb.SmdManageReq_Faulty:
			if len(devs) != 1 {
				return nil, errors.New("set-faulty request expects only one device ID")
			}
			dReq := req.GetFaulty()
			svc.log.Debugf(msg, "set-faulty", dReq)
			devRes, err := sendManageReq(ctx, engine, drpc.MethodSetFaultyState, dReq)
			if err != nil {
				return nil, err
			}
			addManageRespIDOnFail(svc.log, devRes, devs[0])
			devResults = append(devResults, devRes)
		case *ctlpb.SmdManageReq_Led:
			if len(devs) == 0 {
				return nil, errors.New("led-manage request expects one or more IDs")
			}
			// Multiple addresses are supported in LED request.
			for _, dev := range devs {
				dReq := req.GetLed()
				// ID should by now have been resolved to a transport (PCI) address.
				if dev.trAddr == "" {
					return nil, errors.Errorf("device uuid %s not resolved to a PCI address",
						dev.uuid)
				}
				dReq.Ids = dev.trAddr
				svc.log.Debugf(msg, "led-manage", dReq)
				devRes, err := sendManageReq(ctx, engine, drpc.MethodLedManage, dReq)
				if err != nil {
					return nil, err
				}
				addManageRespIDOnFail(svc.log, devRes, dev)
				devResults = append(devResults, devRes)
			}
		default:
			return nil, errors.New("unexpected smd manage request type")
		}

		rankResps = append(rankResps, &ctlpb.SmdManageResp_RankResp{
			Rank: rank.Uint32(), Results: devResults,
		})
	}

	sort.Slice(rankResps, func(i, j int) bool {
		return rankResps[i].Rank < rankResps[j].Rank
	})

	resp := &ctlpb.SmdManageResp{Ranks: rankResps}

	return resp, nil
}
