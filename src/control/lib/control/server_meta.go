//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"fmt"
	"strings"

	"github.com/pkg/errors"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// SmdManageOpcode defines an SmdManage operation.
type SmdManageOpcode uint8

// SmdManageOpcode definitions
const (
	_ SmdManageOpcode = iota
	SetFaultyOp
	DevReplaceOp
	LedCheckOp
	LedBlinkOp
	LedResetOp
)

type (
	// SmdPool contains the per-server components of a DAOS pool.
	SmdPool struct {
		UUID      string        `json:"uuid"`
		TargetIDs []int32       `hash:"set" json:"tgt_ids"`
		Blobs     []uint64      `hash:"set" json:"blobs"`
		Rank      ranklist.Rank `hash:"set" json:"rank"`
	}

	// SmdPoolMap provides a map from pool UUIDs to per-rank pool info.
	SmdPoolMap map[string][]*SmdPool

	// SmdInfo encapsulates SMD-specific information.
	SmdInfo struct {
		Devices []*storage.SmdDevice `hash:"set" json:"devices"`
		Pools   SmdPoolMap           `json:"pools"`
	}

	// SmdQueryReq contains the request parameters for a SMD query operation.
	SmdQueryReq struct {
		unaryRequest
		OmitDevices      bool          `json:"omit_devices"`
		OmitPools        bool          `json:"omit_pools"`
		IncludeBioHealth bool          `json:"include_bio_health"`
		UUID             string        `json:"uuid"`
		Rank             ranklist.Rank `json:"rank"`
		FaultyDevsOnly   bool          `json:"-"` // only show faulty devices
	}

	// SmdManageReq contains the request parameters for a SMD query operation.
	SmdManageReq struct {
		unaryRequest
		IDs             string // comma separated list of IDs
		Rank            ranklist.Rank
		ReplaceUUID     string // For device replacement, UUID of new device
		ReplaceNoReint  bool   // For device replacement, indicate no reintegration
		IdentifyTimeout uint32 // For LED identify, blink duration in minutes
		Operation       SmdManageOpcode
	}

	// SmdResp represents the results of performing SMD query or manage operations across
	// a set of hosts.
	SmdResp struct {
		HostErrorsResp
		HostStorage HostStorageMap `json:"host_storage_map"`
	}
)

func (si *SmdInfo) addRankPools(rank ranklist.Rank, pools []*SmdPool) {
	for _, pool := range pools {
		if _, found := si.Pools[pool.UUID]; !found {
			si.Pools[pool.UUID] = make([]*SmdPool, 0, 1)
		}
		pool.Rank = rank
		si.Pools[pool.UUID] = append(si.Pools[pool.UUID], pool)
	}
}

func (si *SmdInfo) String() string {
	return fmt.Sprintf("[Devices: %v, Pools: %v]", si.Devices, si.Pools)
}

func (sr *SmdResp) addHostQueryResponse(hr *HostResponse, faultyOnly bool) error {
	pbResp, ok := hr.Message.(*ctlpb.SmdQueryResp)
	if !ok {
		return errors.Errorf("unable to unpack message: %+v", hr.Message)
	}

	hs := &HostStorage{
		SmdInfo: &SmdInfo{
			Pools: make(SmdPoolMap),
		},
	}
	for _, rResp := range pbResp.GetRanks() {
		rank := ranklist.Rank(rResp.Rank)

		for _, pbDev := range rResp.GetDevices() {
			isEvicted := pbDev.Ctrlr.DevState == ctlpb.NvmeDevState_EVICTED
			if faultyOnly && !isEvicted {
				continue
			}

			sd := new(storage.SmdDevice)
			if err := convert.Types(pbDev, sd); err != nil {
				return errors.Wrapf(err, "converting %T to %T", pbDev, sd)
			}
			sd.Rank = rank

			hs.SmdInfo.Devices = append(hs.SmdInfo.Devices, sd)
		}

		rPools := make([]*SmdPool, len(rResp.GetPools()))
		if err := convert.Types(rResp.GetPools(), &rPools); err != nil {
			return errors.Wrapf(err, "converting %T to %T", rResp.Pools, &rPools)
		}
		hs.SmdInfo.addRankPools(rank, rPools)
	}

	if sr.HostStorage == nil {
		sr.HostStorage = make(HostStorageMap)
	}
	if err := sr.HostStorage.Add(hr.Addr, hs); err != nil {
		return err
	}

	return nil
}

// SmdQuery concurrently performs per-server metadata operations across all
// hosts supplied in the request's hostlist, or all configured hosts if not
// explicitly specified. The function blocks until all results (successful
// or otherwise) are received, and returns a single response structure
// containing results for all SMD operations.
func SmdQuery(ctx context.Context, rpcClient UnaryInvoker, req *SmdQueryReq) (*SmdResp, error) {
	rpcClient.Debugf("SmdQuery() called with request %+v", req)

	if req == nil {
		return nil, errors.New("nil request")
	}
	if req.UUID != "" {
		if err := checkUUID(req.UUID); err != nil {
			return nil, errors.Wrap(err, "invalid UUID")
		}
	}

	pbReq := new(ctlpb.SmdQueryReq)
	if err := convert.Types(req, pbReq); err != nil {
		return nil, errors.Wrap(err, "unable to convert request to protobuf")
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return ctlpb.NewCtlSvcClient(conn).SmdQuery(ctx, pbReq)
	})

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	sr := new(SmdResp)
	for _, hostResp := range ur.Responses {
		if hostResp.Error != nil {
			if err := sr.addHostError(hostResp.Addr, hostResp.Error); err != nil {
				return nil, err
			}
			continue
		}

		if err := sr.addHostQueryResponse(hostResp, req.FaultyDevsOnly); err != nil {
			return nil, err
		}
	}

	return sr, nil
}

func (sr *SmdResp) addHostManageResponse(hr *HostResponse) error {
	pbResp, ok := hr.Message.(*ctlpb.SmdManageResp)
	if !ok {
		return errors.Errorf("unable to unpack message: %+v", hr.Message)
	}

	var sds []*storage.SmdDevice
	for _, rResp := range pbResp.GetRanks() {
		rank := ranklist.Rank(rResp.Rank)

		for _, pbResult := range rResp.GetResults() {
			if pbResult.Device == nil {
				continue
			}
			sd := new(storage.SmdDevice)
			if err := convert.Types(pbResult.Device, sd); err != nil {
				return errors.Wrapf(err, "converting %T to %T", pbResult.Device, sd)
			}
			sd.Rank = rank
			sds = append(sds, sd)
		}
	}

	hs := &HostStorage{}
	if len(sds) != 0 {
		hs.SmdInfo = &SmdInfo{Devices: sds}
	}

	if sr.HostStorage == nil {
		sr.HostStorage = make(HostStorageMap)
	}
	if err := sr.HostStorage.Add(hr.Addr, hs); err != nil {
		return err
	}

	return nil
}

func (sr *SmdResp) getHostManageRespErr(hr *HostResponse) error {
	pbResp, ok := hr.Message.(*ctlpb.SmdManageResp)
	if !ok {
		return errors.Errorf("unable to unpack message: %+v", hr.Message)
	}

	var errMsgs []string
	for _, rResp := range pbResp.GetRanks() {
		for _, pbResult := range rResp.GetResults() {
			if pbResult.Status == 0 {
				continue
			}
			var id string
			if pbResult.Device != nil {
				id = pbResult.Device.Uuid
				if id == "" {
					id = pbResult.Device.Ctrlr.PciAddr
				}
				id += " "
			}
			errMsgs = append(errMsgs, fmt.Sprintf("rank %d: %s%s", rResp.Rank, id,
				daos.Status(pbResult.Status)))
		}
	}

	if len(errMsgs) > 0 {
		return errors.New(strings.Join(errMsgs, ", "))
	}

	return nil
}

// Pack SmdManage proto request oneof field.
func packPBSmdManageReq(req *SmdManageReq, pbReq *ctlpb.SmdManageReq) error {
	switch req.Operation {
	case SetFaultyOp:
		// Expect single UUID in request IDs field.
		if err := checkUUID(req.IDs); err != nil {
			return errors.Wrap(err, "bad device UUID to set-faulty")
		}
		pbReq.Op = &ctlpb.SmdManageReq_Faulty{
			Faulty: &ctlpb.SetFaultyReq{
				Uuid: req.IDs,
			},
		}
	case DevReplaceOp:
		// Expect single UUID in request IDs field.
		if err := checkUUID(req.IDs); err != nil {
			return errors.Wrap(err, "bad old device UUID for replacement")
		}
		if err := checkUUID(req.ReplaceUUID); err != nil {
			return errors.Wrap(err, "bad new device UUID for replacement")
		}
		pbReq.Op = &ctlpb.SmdManageReq_Replace{
			Replace: &ctlpb.DevReplaceReq{
				OldDevUuid: req.IDs,
				NewDevUuid: req.ReplaceUUID,
				NoReint:    req.ReplaceNoReint,
			},
		}
	case LedCheckOp:
		pbReq.Op = &ctlpb.SmdManageReq_Led{
			Led: &ctlpb.LedManageReq{
				Ids:       req.IDs,
				LedState:  ctlpb.LedState_NA,
				LedAction: ctlpb.LedAction_GET,
			},
		}
	case LedBlinkOp:
		pbReq.Op = &ctlpb.SmdManageReq_Led{
			Led: &ctlpb.LedManageReq{
				Ids:             req.IDs,
				LedState:        ctlpb.LedState_QUICK_BLINK,
				LedAction:       ctlpb.LedAction_SET,
				LedDurationMins: req.IdentifyTimeout,
			},
		}
	case LedResetOp:
		pbReq.Op = &ctlpb.SmdManageReq_Led{
			Led: &ctlpb.LedManageReq{
				Ids:       req.IDs,
				LedState:  ctlpb.LedState_NA,
				LedAction: ctlpb.LedAction_RESET,
			},
		}
	default:
		return errors.New("smd manage called but unrecognized operation requested")
	}

	return nil
}

// SmdManage concurrently performs per-server metadata operations across all
// hosts supplied in the request's hostlist, or all configured hosts if not
// explicitly specified. The function blocks until all results (successful
// or otherwise) are received, and returns a single response structure
// containing results for all SMD operations.
func SmdManage(ctx context.Context, rpcClient UnaryInvoker, req *SmdManageReq) (*SmdResp, error) {
	rpcClient.Debugf("SmdManage() called with request %+v", req)

	if req == nil {
		return nil, errors.New("nil request")
	}

	pbReq := new(ctlpb.SmdManageReq)
	if err := packPBSmdManageReq(req, pbReq); err != nil {
		return nil, errors.Wrap(err, "packing proto manage request")
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return ctlpb.NewCtlSvcClient(conn).SmdManage(ctx, pbReq)
	})

	if req.Operation == SetFaultyOp {
		reqHosts, err := getRequestHosts(DefaultConfig(), req)
		if err != nil {
			return nil, err
		}
		if len(reqHosts) > 1 {
			return nil, errors.New("cannot perform SetFaulty operation on > 1 host")
		}
	}

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	sr := new(SmdResp)
	for _, hostResp := range ur.Responses {
		if hostResp.Error != nil {
			if err := sr.addHostError(hostResp.Addr, hostResp.Error); err != nil {
				return nil, err
			}
			continue
		}

		if respError := sr.getHostManageRespErr(hostResp); respError != nil {
			if err := sr.addHostError(hostResp.Addr, respError); err != nil {
				return nil, err
			}
		}

		if err := sr.addHostManageResponse(hostResp); err != nil {
			return nil, err
		}
	}

	return sr, nil
}
