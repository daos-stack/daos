//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"strconv"
	"strings"

	uuid "github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

func queryRank(reqRank uint32, engineRank system.Rank) bool {
	rr := system.Rank(reqRank)
	if rr.Equals(system.NilRank) {
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

		if req.Target != "" {
			reqTgtID, err := strconv.ParseInt(req.Target, 10, 32)
			if err != nil {
				return errors.Errorf("invalid target idx %q", req.Target)
			}

			found := false
			for _, dev := range rResp.Devices {
				for _, tgtID := range dev.Details.TgtIds {
					if int32(reqTgtID) == tgtID {
						rResp.Devices = []*ctlpb.SmdQueryResp_SmdDeviceWithHealth{dev}
						found = true
						break
					}
				}
			}
			if !found {
				rResp.Devices = nil
			}
		}

		i := 0 // output index
		for _, dev := range rResp.Devices {
			state := storage.NvmeDevStateFromString(dev.Details.DevState)

			svc.log.Debugf("mask %d, state %s", req.StateMask, state)
			if req.StateMask != 0 && req.StateMask&state.Uint32() == 0 {
				continue // skip device completely if mask doesn't match
			}

			// skip health query if the device is not in a normal or faulty state
			if req.IncludeBioHealth {
				if state == storage.NvmeStateNormal || state == storage.NvmeStateFaulty {
					health, err := ei.GetBioHealth(ctx, &ctlpb.BioHealthReq{
						DevUuid: dev.Details.Uuid,
					})
					if err != nil {
						return errors.Wrapf(err, "device %q, states %q",
							dev, state.States())
					}
					dev.Health = health
				} else {
					svc.log.Debugf("skip fetching health stats on device %q, states %q",
						dev, state.States())
				}
			}

			if req.StateMask != 0 {
				// as mask is set and matches state, rewrite slice in place
				rResp.Devices[i] = dev
				i++
			}
		}
		if req.StateMask != 0 {
			// prevent memory leak by erasing truncated values
			for j := i; j < len(rResp.Devices); j++ {
				rResp.Devices[j] = nil
			}
			rResp.Devices = rResp.Devices[:i]
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

func (svc *ControlService) smdQueryDevice(ctx context.Context, req *ctlpb.SmdQueryReq) (*system.Rank, *ctlpb.SmdQueryResp_SmdDeviceWithHealth, error) {
	rank := system.NilRank
	if req.Uuid == "" {
		return nil, nil, errors.New("empty UUID in device query")
	}

	resp := new(ctlpb.SmdQueryResp)
	if err := svc.querySmdDevices(ctx, req, resp); err != nil {
		return nil, nil, err
	}

	for _, rr := range resp.Ranks {
		switch len(rr.Devices) {
		case 0:
			continue
		case 1:
			dev := rr.Devices[0]
			if dev == nil {
				return nil, nil, errors.New("nil device in smd query resp")
			}
			if dev.Details == nil {
				return nil, nil, errors.New("device with nil details in smd query resp")
			}
			svc.log.Debugf("smdQueryDevice(): uuid %q, rank %d, states %q", req.Uuid,
				rr.Rank, rr.Devices[0].Details.DevState)
			rank = system.Rank(rr.Rank)

			return &rank, rr.Devices[0], nil
		default:
			return nil, nil, errors.Errorf("device query on %s matched multiple devices", req.Uuid)
		}
	}

	return &rank, nil, nil
}

func (svc *ControlService) smdGetEngine(ctx context.Context, req *ctlpb.SmdQueryReq) (*system.Rank, *Engine, error) {
	req.Rank = uint32(system.NilRank)
	rank, device, err := svc.smdQueryDevice(ctx, req)
	if err != nil {
		return nil, nil, err
	}
	if device == nil {
		return nil, nil, errors.Errorf("%s did not match any devices", req.Uuid)
	}

	svc.log.Debugf("looking for rank %d instance", rank.Uint32())

	eis, err := svc.harness.FilterInstancesByRankSet(fmt.Sprintf("%d", rank.Uint32()))
	if err != nil {
		return nil, nil, err
	}
	if len(eis) == 0 {
		return nil, nil, errors.Errorf("failed to retrieve instance for rank %d", rank.Uint32())
	}

	return rank, &eis[0], nil
}

// Convert SmdQueryReq to DevManageReq by setting LED state and action based on request modifiers.
func smdQueryToDevManageReq(req *ctlpb.SmdQueryReq) (*ctlpb.DevManageReq, error) {
	dreq := &ctlpb.DevManageReq{
		LedState: ctlpb.VmdLedState_NA,
	}

	switch {
	case req.GetLed:
		dreq.LedAction = ctlpb.VmdLedAction_GET
	case req.SetFaulty:
		// LED action and target state will be populated in dRPC handler.
	case req.Identify:
		dreq.LedState = ctlpb.VmdLedState_QUICK_BLINK
		dreq.LedAction = ctlpb.VmdLedAction_SET
	case req.ResetLed:
		dreq.LedAction = ctlpb.VmdLedAction_RESET
	default:
		return nil, errors.New("smd manage called without modifier set in request")
	}

	return dreq, nil
}

// Split IDs in comma separated string and assign each token to relevant return list.
func getLEDReqIDs(log logging.Logger, ids string) (map[string]bool, map[string]bool, error) {
	if ids == "" {
		return nil, nil, errors.New("empty id string")
	}

	tokens := strings.Split(ids, ",")

	addrs := make(map[string]bool)
	uuids := make(map[string]bool)

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

type engineDevMap map[*Engine][]devID

// Map requested device IDs provided in comma-separated string to the engine that controls the given
// device. Device can be identified either by UUID or transport (PCI) address.
func (svc *ControlService) smdMapDevIDsToEngine(ctx context.Context, ids string) (engineDevMap, error) {
	// Special case for smdManage where list of IDs have been passed in UUID field.
	trAddrs, devUUIDs, err := getLEDReqIDs(svc.log, ids)
	if err != nil {
		return nil, err
	}

	req := &ctlpb.SmdQueryReq{Rank: uint32(system.NilRank)}
	resp := new(ctlpb.SmdQueryResp)
	if err := svc.querySmdDevices(ctx, req, resp); err != nil {
		return nil, err
	}

	edm := make(engineDevMap)

	for _, rr := range resp.Ranks {
		seenTrAddrs := make(map[string]bool)
		seenUuids := make(map[string]bool)

		eis, err := svc.harness.FilterInstancesByRankSet(fmt.Sprintf("%d", rr.Rank))
		if err != nil {
			return nil, err
		}
		if len(eis) == 0 {
			return nil, errors.Errorf("failed to retrieve instance for rank %d", rr.Rank)
		}
		eisPtr := &eis[0]

		edm[eisPtr] = []devID{}

		for _, dev := range rr.Devices {
			if dev == nil {
				return nil, errors.New("nil device in smd query resp")
			}
			dds := dev.Details
			if dds == nil {
				return nil, errors.New("device with nil details in smd query resp")
			}
			svc.log.Debugf("smdQueryDevice(): %+v", *dds)

			if seenTrAddrs[dds.TrAddr] || seenUuids[dds.Uuid] {
				// Skip if entry already exists for this TrAddr or UUID.
				svc.log.Debugf("skip device as entry has already been added")
				continue
			}

			// Where possible specify the TrAddr over UUID as there may be multiple
			// UUIDs mapping to the same TrAddr.
			if dds.TrAddr != "" {
				if trAddrs[dds.TrAddr] || (dds.Uuid != "" && devUUIDs[dds.Uuid]) {
					// If UUID matches, add by TrAddr rather than UUID which
					// should avoid duplicate UUID entries for the same TrAddr.
					edm[eisPtr] = append(edm[eisPtr], devID{trAddr: dds.TrAddr})
					seenTrAddrs[dds.TrAddr] = true
					svc.log.Debugf("adding %s to drpc request list", dds.TrAddr)
					continue
				}
			}

			if dds.Uuid != "" && devUUIDs[dds.Uuid] {
				// Only add UUID entry if TrAddr is not available for a device.
				edm[eisPtr] = append(edm[eisPtr], devID{uuid: dds.Uuid})
				seenUuids[dds.Uuid] = true
				svc.log.Debugf("adding %s to drpc request list", dds.Uuid)
			}
		}
	}

	return edm, nil
}

func (svc *ControlService) smdManage(ctx context.Context, req *ctlpb.SmdQueryReq) (*ctlpb.SmdQueryResp, error) {
	dmeth := drpc.MethodLedManage
	if req.SetFaulty {
		dmeth = drpc.MethodSetFaultyState
	}

	dreq, err := smdQueryToDevManageReq(req)
	if err != nil {
		return nil, err
	}

	engineDevMap, err := svc.smdMapDevIDsToEngine(ctx, req.Uuid)
	if err != nil {
		return nil, errors.Wrap(err, "mapping device identifiers to engine")
	}

	rankResps := []*ctlpb.SmdQueryResp_RankResp{}

	for engine, devs := range engineDevMap {
		respDevs := []*ctlpb.SmdQueryResp_SmdDeviceWithHealth{}

		rank, err := (*engine).GetRank()
		if err != nil {
			return nil, errors.Wrap(err, "retrieving engine rank")
		}

		for _, dev := range devs {
			switch {
			case dev.trAddr != "":
				dreq.TrAddr = dev.trAddr
			case dev.uuid != "":
				dreq.Uuid = dev.uuid
			default:
				return nil, errors.New("no id found in engine device map value")
			}

			svc.log.Debugf("calling dev manage drpc on rank %d (req: %+v)", rank, *dreq)

			dresp, err := (*engine).CallDrpc(ctx, dmeth, dreq)
			if err != nil {
				return nil, errors.Wrapf(err, "call drpc method %q, req %+v",
					dmeth, dreq)
			}

			dmr := &ctlpb.DevManageResp{}
			if err = proto.Unmarshal(dresp.Body, dmr); err != nil {
				return nil, errors.Wrapf(err, "unmarshal %T response", dmr)
			}
			if dmr.Status != 0 {
				svc.log.Errorf("%q returned status %q, req %+v", dmeth,
					daos.Status(dmr.Status), dreq)
			}

			respDevs = append(respDevs, &ctlpb.SmdQueryResp_SmdDeviceWithHealth{
				Status: dmr.Status, Details: dmr.Device,
			})
		}

		rankResps = append(rankResps, &ctlpb.SmdQueryResp_RankResp{
			Rank: rank.Uint32(), Devices: respDevs,
		})
	}

	return &ctlpb.SmdQueryResp{Ranks: rankResps}, nil
}

func (svc *ControlService) smdReplace(ctx context.Context, req *ctlpb.SmdQueryReq) (*ctlpb.SmdQueryResp, error) {
	rank, eis, err := svc.smdGetEngine(ctx, req)
	if err != nil {
		return nil, errors.Wrap(err, "deriving target engine for smd manage drpc")
	}

	svc.log.Debugf("calling storage replace on rank %d for %s", rank.Uint32(), req.Uuid)

	dresp, err := (*eis).CallDrpc(ctx, drpc.MethodReplaceStorage, &ctlpb.DevReplaceReq{
		OldDevUuid: req.Uuid,
		NewDevUuid: req.ReplaceUuid,
		NoReint:    req.NoReint,
	})
	if err != nil {
		return nil, err
	}

	drr := &ctlpb.DevReplaceResp{}
	if err = proto.Unmarshal(dresp.Body, drr); err != nil {
		return nil, errors.Wrap(err, "unmarshal StorageReplace response")
	}
	if drr.Status != 0 {
		return nil, errors.Wrap(daos.Status(drr.Status), "smdReplace failed")
	}
	if drr.Device == nil {
		return nil, errors.New("nil device pointer in DevReplaceResp")
	}

	return &ctlpb.SmdQueryResp{
		Ranks: []*ctlpb.SmdQueryResp_RankResp{
			{
				Rank:    rank.Uint32(),
				Devices: []*ctlpb.SmdQueryResp_SmdDeviceWithHealth{{Details: drr.Device}},
			},
		},
	}, nil
}

// SmdQuery implements the method defined for the Management Service.
//
// Query SMD info for pools or devices.
func (svc *ControlService) SmdQuery(ctx context.Context, req *ctlpb.SmdQueryReq) (*ctlpb.SmdQueryResp, error) {
	svc.log.Debugf("CtlSvc.SmdQuery dispatch, req:%+v\n", *req)

	if !svc.harness.isStarted() {
		return nil, FaultHarnessNotStarted
	}
	if len(svc.harness.readyRanks()) == 0 {
		return nil, FaultDataPlaneNotStarted
	}

	if req.SetFaulty || req.Identify || req.ResetLed || req.GetLed {
		return svc.smdManage(ctx, req)
	}

	if req.ReplaceUuid != "" {
		return svc.smdReplace(ctx, req)
	}

	if req.Uuid != "" && (!req.OmitDevices && !req.OmitPools) {
		return nil, errors.New("UUID is ambiguous when querying both pools and devices")
	}
	if req.Target != "" && req.Rank == uint32(system.NilRank) {
		return nil, errors.New("Target is invalid without Rank")
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

	svc.log.Debugf("CtlSvc.SmdQuery dispatch, resp:%+v\n", *resp)
	return resp, nil
}
