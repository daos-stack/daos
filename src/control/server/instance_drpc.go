//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"

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

func (srv *IOServerInstance) setDrpcClient(c drpc.DomainSocketClient) {
	srv.Lock()
	defer srv.Unlock()
	srv._drpcClient = c
}

func (srv *IOServerInstance) getDrpcClient() (drpc.DomainSocketClient, error) {
	srv.RLock()
	defer srv.RUnlock()
	if srv._drpcClient == nil {
		return nil, errDRPCNotReady
	}
	return srv._drpcClient, nil
}

// NotifyDrpcReady receives a ready message from the running IOServer
// instance.
func (srv *IOServerInstance) NotifyDrpcReady(msg *srvpb.NotifyReadyReq) {
	srv.log.Debugf("%s instance %d drpc ready: %v", build.DataPlaneName, srv.Index(), msg)

	// Activate the dRPC client connection to this iosrv
	srv.setDrpcClient(drpc.NewClientConnection(msg.DrpcListenerSock))

	go func() {
		srv.drpcReady <- msg
	}()
}

// awaitDrpcReady returns a channel which receives a ready message
// when the started IOServer instance indicates that it is
// ready to receive dRPC messages.
func (srv *IOServerInstance) awaitDrpcReady() chan *srvpb.NotifyReadyReq {
	return srv.drpcReady
}

// CallDrpc makes the supplied dRPC call via this instance's dRPC client.
func (srv *IOServerInstance) CallDrpc(ctx context.Context, method drpc.Method, body proto.Message) (*drpc.Response, error) {
	dc, err := srv.getDrpcClient()
	if err != nil {
		return nil, err
	}

	rankMsg := ""
	if sb := srv.getSuperblock(); sb != nil {
		rankMsg = fmt.Sprintf(" (rank %s)", sb.Rank)
	}
	srv.log.Debugf("dRPC to index %d%s: %s", srv.Index(), rankMsg, method)

	return makeDrpcCall(ctx, srv.log, dc, method, body)
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

// TryDrpc attempts dRPC request to given rank managed by instance and return
// success or error from call result or timeout encapsulated in result.
func (srv *IOServerInstance) TryDrpc(ctx context.Context, method drpc.Method) *system.MemberResult {
	rank, err := srv.GetRank()
	if err != nil {
		return nil // no rank to return result for
	}

	localState := srv.LocalState()
	if localState != system.MemberStateReady {
		// member not ready for dRPC comms, annotate result with last
		// error as Msg field if found to be stopped
		result := &system.MemberResult{Rank: rank, State: localState}
		if localState == system.MemberStateStopped && srv._lastErr != nil {
			result.Msg = srv._lastErr.Error()
		}
		return result
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
		dresp, err := srv.CallDrpc(ctx, method, nil)
		resChan <- drespToMemberResult(rank, dresp, err, targetState)
	}()

	select {
	case <-ctx.Done():
		if ctx.Err() == context.DeadlineExceeded {
			return &system.MemberResult{
				Rank: rank, Msg: ctx.Err().Error(),
				State: system.MemberStateUnresponsive,
			}
		}
		return nil // shutdown
	case result := <-resChan:
		return result
	}
}

func (srv *IOServerInstance) getBioHealth(ctx context.Context, req *ctlpb.BioHealthReq) (*ctlpb.BioHealthResp, error) {
	dresp, err := srv.CallDrpc(ctx, drpc.MethodBioHealth, req)
	if err != nil {
		return nil, err
	}

	resp := &ctlpb.BioHealthResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal BioHealthQuery response")
	}

	if resp.Status != 0 {
		return nil, errors.Wrap(drpc.DaosStatus(resp.Status), "getBioHealth failed")
	}

	return resp, nil
}

func (srv *IOServerInstance) listSmdDevices(ctx context.Context, req *ctlpb.SmdDevReq) (*ctlpb.SmdDevResp, error) {
	dresp, err := srv.CallDrpc(ctx, drpc.MethodSmdDevs, req)
	if err != nil {
		return nil, err
	}

	resp := new(ctlpb.SmdDevResp)
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal SmdListDevs response")
	}

	if resp.Status != 0 {
		return nil, errors.Wrap(drpc.DaosStatus(resp.Status), "listSmdDevices failed")
	}

	return resp, nil
}

// updateInUseBdevs updates-in-place the input list of controllers with
// new NVMe health stats and SMD metadata info.
//
// Query each SmdDevice on each IO server instance for health stats,
// map input controllers to their concatenated model+serial keys then
// retrieve metadata and health stats for each SMD device (blobstore) on
// a given I/O server instance. Update input map with new stats/smd info.
func (srv *IOServerInstance) updateInUseBdevs(ctx context.Context, ctrlrMap map[string]*storage.NvmeController) error {
	smdDevs, err := srv.listSmdDevices(ctx, new(ctlpb.SmdDevReq))
	if err != nil {
		return errors.Wrapf(err, "instance %d listSmdDevices()", srv.Index())
	}

	hasUpdatedHealth := make(map[string]bool)
	for _, dev := range smdDevs.Devices {
		msg := fmt.Sprintf("instance %d: smd %s with transport address %s",
			srv.Index(), dev.GetUuid(), dev.GetTrAddr())

		ctrlr, exists := ctrlrMap[dev.GetTrAddr()]
		if !exists {
			return errors.Errorf("%s: didn't match any known controllers", msg)
		}

		pbStats, err := srv.getBioHealth(ctx, &ctlpb.BioHealthReq{
			DevUuid: dev.GetUuid(),
		})
		if err != nil {
			return errors.Wrapf(err, "instance %d getBioHealth()", srv.Index())
		}

		health := new(storage.NvmeHealth)
		if err := convert.Types(pbStats, health); err != nil {
			return errors.Wrapf(err, msg)
		}

		// multiple updates for the same key expected when
		// more than one controller namespaces (and resident
		// blobstores) exist, stats will be the same for each
		if _, already := hasUpdatedHealth[ctrlr.PciAddr]; !already {
			ctrlr.HealthStats = health
			msg = fmt.Sprintf("%s: health stats updated", msg)
			hasUpdatedHealth[ctrlr.PciAddr] = true
		}

		smdDev := new(storage.SmdDevice)
		if err := convert.Types(dev, smdDev); err != nil {
			return errors.Wrapf(err, "convert smd for ctrlr %s", ctrlr.PciAddr)
		}
		srvRank, err := srv.GetRank()
		if err != nil {
			return errors.Wrapf(err, "get rank")
		}
		smdDev.Rank = srvRank
		smdDev.TrAddr = dev.GetTrAddr()
		// space utilization stats for each smd device
		smdDev.TotalBytes = pbStats.TotalBytes
		smdDev.AvailBytes = pbStats.AvailBytes

		ctrlr.UpdateSmd(smdDev)
		srv.log.Debugf("%s: smd usage updated", msg)
	}

	return nil
}
