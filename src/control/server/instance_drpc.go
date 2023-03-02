//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"time"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

var (
	errDRPCNotReady     = errors.New("no dRPC client set (data plane not started?)")
	errInstanceNotReady = errors.New("instance not ready yet")
)

func (ei *EngineInstance) setDrpcClient(c drpc.DomainSocketClient) {
	ei.Lock()
	defer ei.Unlock()
	ei._drpcClient = c
}

func (ei *EngineInstance) getDrpcClient() (drpc.DomainSocketClient, error) {
	ei.RLock()
	defer ei.RUnlock()
	if ei._drpcClient == nil {
		return nil, errDRPCNotReady
	}
	return ei._drpcClient, nil
}

// NotifyDrpcReady receives a ready message from the running Engine
// instance.
func (ei *EngineInstance) NotifyDrpcReady(msg *srvpb.NotifyReadyReq) {
	ei.log.Debugf("%s instance %d drpc ready: %v", build.DataPlaneName, ei.Index(), msg)

	// activate the dRPC client connection to this engine
	ei.setDrpcClient(drpc.NewClientConnection(msg.DrpcListenerSock))

	go func() {
		ei.drpcReady <- msg
	}()
}

// awaitDrpcReady returns a channel which receives a ready message
// when the started Engine instance indicates that it is
// ready to receive dRPC messages.
func (ei *EngineInstance) awaitDrpcReady() chan *srvpb.NotifyReadyReq {
	return ei.drpcReady
}

// CallDrpc makes the supplied dRPC call via this instance's dRPC client.
func (ei *EngineInstance) CallDrpc(ctx context.Context, method drpc.Method, body proto.Message) (*drpc.Response, error) {
	dc, err := ei.getDrpcClient()
	if err != nil {
		return nil, err
	}

	rankMsg := ""
	if sb := ei.getSuperblock(); sb != nil && sb.Rank != nil {
		rankMsg = fmt.Sprintf(" (rank %s)", sb.Rank)
	}

	startedAt := time.Now()
	defer func() {
		ei.log.Debugf("dRPC to index %d%s: %s/%dB/%s", ei.Index(), rankMsg, method, proto.Size(body), time.Since(startedAt))
	}()

	return makeDrpcCall(ctx, ei.log, dc, method, body)
}

// drespToMemberResult converts drpc.Response to system.MemberResult.
//
// MemberResult is populated with rank, state and error dependent on processing
// dRPC response. Target state param is populated on success, Errored otherwise.
func drespToMemberResult(rank system.Rank, dresp *drpc.Response, err error, tState system.MemberState) *system.MemberResult {
	if err != nil {
		return system.NewMemberResult(rank,
			errors.WithMessagef(err, "rank %s dRPC failed", &rank),
			system.MemberStateErrored)
	}

	resp := &mgmtpb.DaosResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return system.NewMemberResult(rank,
			errors.Errorf("rank %s dRPC unmarshal failed", &rank),
			system.MemberStateErrored)
	}
	if resp.GetStatus() != 0 {
		return system.NewMemberResult(rank,
			errors.Errorf("rank %s: %s", &rank, drpc.DaosStatus(resp.GetStatus()).Error()),
			system.MemberStateErrored)
	}

	return system.NewMemberResult(rank, nil, tState)
}

// tryDrpc attempts dRPC request to given rank managed by instance and return
// success or error from call result or timeout encapsulated in result.
func (ei *EngineInstance) tryDrpc(ctx context.Context, method drpc.Method) *system.MemberResult {
	rank, err := ei.GetRank()
	if err != nil {
		return nil // no rank to return result for
	}

	localState := ei.LocalState()
	if localState != system.MemberStateReady {
		// member not ready for dRPC comms, annotate result with last error if stopped
		var err error
		if localState == system.MemberStateStopped && ei._lastErr != nil {
			err = ei._lastErr
		}
		return system.NewMemberResult(rank, err, localState)
	}

	// system member state that should be set on dRPC success
	targetState := system.MemberStateUnknown
	switch method {
	case drpc.MethodPrepShutdown:
		targetState = system.MemberStateStopping
	case drpc.MethodPingRank:
		targetState = system.MemberStateReady
	default:
		return system.NewMemberResult(rank,
			errors.Errorf("unsupported dRPC method (%s) for fanout", method),
			system.MemberStateErrored)
	}

	resChan := make(chan *system.MemberResult)
	go func() {
		dresp, err := ei.CallDrpc(ctx, method, nil)
		resChan <- drespToMemberResult(rank, dresp, err, targetState)
	}()

	select {
	case <-ctx.Done():
		if ctx.Err() == context.DeadlineExceeded {
			return system.NewMemberResult(rank, ctx.Err(), system.MemberStateUnresponsive)
		}
		return nil // shutdown
	case result := <-resChan:
		return result
	}
}

func (ei *EngineInstance) GetBioHealth(ctx context.Context, req *ctlpb.BioHealthReq) (*ctlpb.BioHealthResp, error) {
	dresp, err := ei.CallDrpc(ctx, drpc.MethodBioHealth, req)
	if err != nil {
		return nil, errors.Wrap(err, "GetBioHealth dRPC call")
	}

	resp := &ctlpb.BioHealthResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal BioHealthQuery response")
	}

	if resp.Status != 0 {
		return nil, errors.Wrap(drpc.DaosStatus(resp.Status), "GetBioHealth response status")
	}

	return resp, nil
}

func (ei *EngineInstance) ListSmdDevices(ctx context.Context, req *ctlpb.SmdDevReq) (*ctlpb.SmdDevResp, error) {
	dresp, err := ei.CallDrpc(ctx, drpc.MethodSmdDevs, req)
	if err != nil {
		return nil, err
	}

	resp := new(ctlpb.SmdDevResp)
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal SmdListDevs response")
	}

	if resp.Status != 0 {
		return nil, errors.Wrap(drpc.DaosStatus(resp.Status), "ListSmdDevices failed")
	}

	return resp, nil
}

func (ei *EngineInstance) getSmdDetails(smd *ctlpb.SmdDevResp_Device) (*storage.SmdDevice, error) {
	smdDev := new(storage.SmdDevice)
	if err := convert.Types(smd, smdDev); err != nil {
		return nil, errors.Wrap(err, "convert smd")
	}

	engineRank, err := ei.GetRank()
	if err != nil {
		return nil, errors.Wrapf(err, "get rank")
	}

	smdDev.Rank = engineRank
	smdDev.TrAddr = smd.GetTrAddr()

	return smdDev, nil
}

func updateCtrlrHealth(pbStats *ctlpb.BioHealthResp, ctrlr *storage.NvmeController) error {
	ctrlr.HealthStats = new(storage.NvmeHealth)
	if err := convert.Types(pbStats, ctrlr.HealthStats); err != nil {
		return errors.Wrap(err, "convert health stats")
	}

	return nil
}

// updateInUseBdevs updates-in-place the input list of controllers with new NVMe health stats and
// SMD metadata info.
//
// Query each SmdDevice on each I/O Engine instance for health stats and update existing controller
// data in ctrlrMap using PCI address key.
func (ei *EngineInstance) updateInUseBdevs(ctx context.Context, ctrlrMap map[string]*storage.NvmeController) (err error) {
	defer func() {
		err = errors.Wrapf(err, "instance %d", ei.Index())
	}()

	// Clear SMD info for controllers in ctrlrMap, populate smd info from scratch.
	for _, ctrlr := range ctrlrMap {
		ctrlr.SmdDevices = []*storage.SmdDevice{}
	}

	smdDevs, err := ei.ListSmdDevices(ctx, new(ctlpb.SmdDevReq))
	if err != nil {
		return errors.Wrapf(err, "list smd devices")
	}
	ei.log.Debugf("engine %d: smdDevs %+v", ei.Index(), smdDevs)

	hasUpdatedHealth := make(map[string]bool)
	for _, smd := range smdDevs.Devices {
		msg := fmt.Sprintf("instance %d: smd %s: ctrlr %s", ei.Index(), smd.Uuid,
			smd.TrAddr)

		ctrlr, exists := ctrlrMap[smd.GetTrAddr()]
		if !exists {
			ei.log.Errorf("%s: ctrlr not found", msg)
			continue
		}

		smdDev, err := ei.getSmdDetails(smd)
		if err != nil {
			return errors.Wrapf(err, "%s: collect smd info", msg)
		}

		pbStats, err := ei.GetBioHealth(ctx, &ctlpb.BioHealthReq{DevUuid: smdDev.UUID})
		if err != nil {
			// Only log error if error indicates non-existent health and the SMD entity
			// has abnormal state.
			status, ok := errors.Cause(err).(drpc.DaosStatus)
			if ok && status == drpc.DaosNonexistant && !smdDev.NvmeState.IsNormal() {
				ei.log.Debugf("%s: stats not found (device state: %q), skip update",
					msg, smdDev.NvmeState.String())
			} else {
				ei.log.Errorf("%s: fetch stats: %s", msg, err.Error())
			}
			ctrlr.UpdateSmd(smdDev)
			continue
		}

		// Populate space usage for each SMD device from health stats.
		smdDev.ClusterSize = pbStats.ClusterSize
		smdDev.TotalBytes = pbStats.TotalBytes
		smdDev.AvailBytes = pbStats.AvailBytes
		msg = fmt.Sprintf("%s: smd usage = %s/%s", msg, humanize.Bytes(smdDev.AvailBytes),
			humanize.Bytes(smdDev.TotalBytes))
		ctrlr.UpdateSmd(smdDev)

		// Multiple SMD entries for the same address key may exist when there are multiple
		// NVMe namespaces (and resident blobstores) exist on a single controller. In this
		// case only update once as health stats will be the same for each.
		if hasUpdatedHealth[ctrlr.PciAddr] {
			continue
		}

		if err := updateCtrlrHealth(pbStats, ctrlr); err != nil {
			ei.log.Errorf("%s: update ctrlr health: %s", err.Error())
			continue
		}
		hasUpdatedHealth[ctrlr.PciAddr] = true

		ei.log.Debugf("%s: ctrlr health updated", msg)
	}

	return nil
}
