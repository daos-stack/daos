//
// (C) Copyright 2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package server

import (
	"context"
	"fmt"
	"strconv"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/system"
)

func queryRank(reqRank uint32, srvRank system.Rank) bool {
	rr := system.Rank(reqRank)
	if rr.Equals(system.NilRank) {
		return true
	}
	return rr.Equals(srvRank)
}

func (svc *mgmtSvc) querySmdDevices(ctx context.Context, req *mgmtpb.SmdQueryReq, resp *mgmtpb.SmdQueryResp) error {
	for _, srv := range svc.harness.Instances() {
		if !srv.isReady() {
			svc.log.Debugf("skipping not-ready instance")
			continue
		}

		srvRank, err := srv.GetRank()
		if err != nil {
			return err
		}
		if !queryRank(req.GetRank(), srvRank) {
			continue
		}

		rResp := new(mgmtpb.SmdQueryResp_RankResp)
		rResp.Rank = srvRank.Uint32()

		listDevsResp, err := srv.listSmdDevices(ctx, new(mgmtpb.SmdDevReq))
		if err != nil {
			return errors.Wrapf(err, "rank %d", srvRank)
		}

		if err := convert.Types(listDevsResp.Devices, &rResp.Devices); err != nil {
			return errors.Wrap(err, "failed to convert device list")
		}
		resp.Ranks = append(resp.Ranks, rResp)

		if req.Uuid != "" {
			found := false
			for _, dev := range rResp.Devices {
				if dev.Uuid == req.Uuid {
					rResp.Devices = []*mgmtpb.SmdQueryResp_Device{dev}
					found = true
					break
				}
			}
			if !found {
				rResp.Devices = nil
			}
		}

		if req.Target != "" {
			reqTgtId, err := strconv.ParseInt(req.Target, 10, 32)
			if err != nil {
				return errors.Errorf("invalid target idx %q", req.Target)
			}

			found := false
			for _, dev := range rResp.Devices {
				for _, tgtId := range dev.TgtIds {
					if int32(reqTgtId) == tgtId {
						rResp.Devices = []*mgmtpb.SmdQueryResp_Device{dev}
						found = true
						break
					}
				}
			}
			if !found {
				rResp.Devices = nil
			}
		}

		if !req.IncludeBioHealth {
			continue
		}

		for _, dev := range rResp.Devices {
			health, err := srv.getBioHealth(ctx, &mgmtpb.BioHealthReq{
				DevUuid: dev.Uuid,
			})
			if err != nil {
				return errors.Wrapf(err, "device %s", dev)
			}
			dev.Health = health
		}
	}
	return nil
}

func (svc *mgmtSvc) querySmdPools(ctx context.Context, req *mgmtpb.SmdQueryReq, resp *mgmtpb.SmdQueryResp) error {
	for _, srv := range svc.harness.Instances() {
		if !srv.isReady() {
			svc.log.Debugf("skipping not-ready instance")
			continue
		}

		srvRank, err := srv.GetRank()
		if err != nil {
			return err
		}
		if !queryRank(req.GetRank(), srvRank) {
			continue
		}

		rResp := new(mgmtpb.SmdQueryResp_RankResp)
		rResp.Rank = srvRank.Uint32()

		dresp, err := srv.CallDrpc(ctx, drpc.MethodSmdPools, new(mgmtpb.SmdPoolReq))
		if err != nil {
			return err
		}

		rankDevResp := new(mgmtpb.SmdPoolResp)
		if err = proto.Unmarshal(dresp.Body, rankDevResp); err != nil {
			return errors.Wrap(err, "unmarshal SmdListPools response")
		}

		if rankDevResp.Status != 0 {
			return errors.Wrapf(drpc.DaosStatus(rankDevResp.Status), "rank %d ListPools failed", srvRank)
		}

		if err := convert.Types(rankDevResp.Pools, &rResp.Pools); err != nil {
			return errors.Wrap(err, "failed to convert pool list")
		}
		resp.Ranks = append(resp.Ranks, rResp)

		if req.Uuid != "" {
			found := false
			for _, pool := range rResp.Pools {
				if pool.Uuid == req.Uuid {
					rResp.Pools = []*mgmtpb.SmdQueryResp_Pool{pool}
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

func (svc *mgmtSvc) smdQueryDevice(ctx context.Context, req *mgmtpb.SmdQueryReq) (system.Rank, *mgmtpb.SmdQueryResp_Device, error) {
	rank := system.NilRank
	if req.Uuid == "" {
		return rank, nil, errors.New("empty UUID in device query")
	}

	resp := new(mgmtpb.SmdQueryResp)
	if err := svc.querySmdDevices(ctx, req, resp); err != nil {
		return rank, nil, err
	}

	for _, rr := range resp.Ranks {
		switch len(rr.Devices) {
		case 0:
			continue
		case 1:
			rank = system.Rank(rr.Rank)
			return rank, rr.Devices[0], nil
		default:
			return rank, nil, errors.Errorf("device query on %s matched multiple devices", req.Uuid)
		}
	}

	return rank, nil, nil
}

func (svc *mgmtSvc) smdSetFaulty(ctx context.Context, req *mgmtpb.SmdQueryReq) (*mgmtpb.SmdQueryResp, error) {
	req.Rank = uint32(system.NilRank)
	rank, device, err := svc.smdQueryDevice(ctx, req)
	if err != nil {
		return nil, err
	}
	if device == nil {
		return nil, errors.Errorf("smdSetFaulty on %s did not match any devices", req.Uuid)
	}

	srvs, err := svc.harness.FilterInstancesByRankSet(fmt.Sprintf("%d", rank))
	if err != nil {
		return nil, err
	}
	if len(srvs) == 0 {
		return nil, errors.Errorf("failed to retrieve instance for rank %d", rank)
	}

	svc.log.Debugf("calling set-faulty on rank %d for %s", rank, req.Uuid)

	dresp, err := srvs[0].CallDrpc(ctx, drpc.MethodSetFaultyState, &mgmtpb.DevStateReq{
		DevUuid: req.Uuid,
	})
	if err != nil {
		return nil, err
	}

	dsr := &mgmtpb.DevStateResp{}
	if err = proto.Unmarshal(dresp.Body, dsr); err != nil {
		return nil, errors.Wrap(err, "unmarshal StorageSetFaulty response")
	}

	if dsr.Status != 0 {
		return nil, errors.Wrap(drpc.DaosStatus(dsr.Status), "smdSetFaulty failed")
	}

	return &mgmtpb.SmdQueryResp{
		Ranks: []*mgmtpb.SmdQueryResp_RankResp{
			{
				Rank: rank.Uint32(),
				Devices: []*mgmtpb.SmdQueryResp_Device{
					{
						Uuid:  dsr.DevUuid,
						State: dsr.DevState,
					},
				},
			},
		},
	}, nil
}

func (svc *mgmtSvc) smdReplace(ctx context.Context, req *mgmtpb.SmdQueryReq) (*mgmtpb.SmdQueryResp, error) {
	req.Rank = uint32(system.NilRank)
	rank, device, err := svc.smdQueryDevice(ctx, req)
	if err != nil {
		return nil, err
	}
	if device == nil {
		return nil, errors.Errorf("smdReplace on %s did not match any devices", req.Uuid)
	}

	srvs, err := svc.harness.FilterInstancesByRankSet(fmt.Sprintf("%d", rank))
	if err != nil {
		return nil, err
	}
	if len(srvs) == 0 {
		return nil, errors.Errorf("failed to retrieve instance for rank %d", rank)
	}

	svc.log.Debugf("calling storage replace on rank %d for %s", rank, req.Uuid)

	dresp, err := srvs[0].CallDrpc(ctx, drpc.MethodReplaceStorage, &mgmtpb.DevReplaceReq{
		OldDevUuid: req.Uuid,
		NewDevUuid: req.ReplaceUUID,
		NoReint:    req.NoReint,
	})
	if err != nil {
		return nil, err
	}

	drr := &mgmtpb.DevReplaceResp{}
	if err = proto.Unmarshal(dresp.Body, drr); err != nil {
		return nil, errors.Wrap(err, "unmarshal StorageReplace response")
	}

	if drr.Status != 0 {
		return nil, errors.Wrap(drpc.DaosStatus(drr.Status), "smdReplace failed")
	}

	return &mgmtpb.SmdQueryResp{
		Ranks: []*mgmtpb.SmdQueryResp_RankResp{
			{
				Rank: rank.Uint32(),
				Devices: []*mgmtpb.SmdQueryResp_Device{
					{
						Uuid:  drr.NewDevUuid,
						State: drr.DevState,
					},
				},
			},
		},
	}, nil
}

func (svc *mgmtSvc) smdIdentify(ctx context.Context, req *mgmtpb.SmdQueryReq) (*mgmtpb.SmdQueryResp, error) {
	req.Rank = uint32(system.NilRank)
	rank, device, err := svc.smdQueryDevice(ctx, req)
	if err != nil {
		return nil, err
	}
	if device == nil {
		return nil, errors.Errorf("smdIdentify on %s did not match any devices", req.Uuid)
	}

	srvs, err := svc.harness.FilterInstancesByRankSet(fmt.Sprintf("%d", rank))
	if err != nil {
		return nil, err
	}
	if len(srvs) == 0 {
		return nil, errors.Errorf("failed to retrieve instance for rank %d", rank)
	}

	svc.log.Debugf("calling storage identify on rank %d for %s", rank, req.Uuid)

	dresp, err := srvs[0].CallDrpc(ctx, drpc.MethodIdentifyStorage, &mgmtpb.DevIdentifyReq{
		DevUuid: req.Uuid,
	})
	if err != nil {
		return nil, err
	}

	drr := &mgmtpb.DevIdentifyResp{}
	if err = proto.Unmarshal(dresp.Body, drr); err != nil {
		return nil, errors.Wrap(err, "unmarshal StorageIdentify response")
	}

	if drr.Status != 0 {
		return nil, errors.Wrap(drpc.DaosStatus(drr.Status), "smdIdentify failed")
	}

	return &mgmtpb.SmdQueryResp{
		Ranks: []*mgmtpb.SmdQueryResp_RankResp{
			{
				Rank: rank.Uint32(),
				Devices: []*mgmtpb.SmdQueryResp_Device{
					{
						Uuid:  drr.DevUuid,
						State: drr.LedState,
					},
				},
			},
		},
	}, nil
}

func (svc *mgmtSvc) SmdQuery(ctx context.Context, req *mgmtpb.SmdQueryReq) (*mgmtpb.SmdQueryResp, error) {
	svc.log.Debugf("MgmtSvc.SmdQuery dispatch, req:%+v\n", *req)

	if !svc.harness.isStarted() {
		return nil, FaultHarnessNotStarted
	}
	if len(svc.harness.readyRanks()) == 0 {
		return nil, FaultDataPlaneNotStarted
	}

	if req.SetFaulty {
		return svc.smdSetFaulty(ctx, req)
	}

	if req.ReplaceUUID != "" {
		return svc.smdReplace(ctx, req)
	}

	if req.Identify {
		return svc.smdIdentify(ctx, req)
	}

	if req.Uuid != "" && (!req.OmitDevices && !req.OmitPools) {
		return nil, errors.New("UUID is ambiguous when querying both pools and devices")
	}
	if req.Target != "" && req.Rank == uint32(system.NilRank) {
		return nil, errors.New("Target is invalid without Rank")
	}

	resp := new(mgmtpb.SmdQueryResp)
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

	svc.log.Debugf("MgmtSvc.SmdQuery dispatch, resp:%+v\n", *resp)
	return resp, nil
}
