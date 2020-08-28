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

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
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
		return nil, errors.New("no dRPC client set (data plane not started?)")
	}
	return srv._drpcClient, nil
}

// NotifyDrpcReady receives a ready message from the running IOServer
// instance.
func (srv *IOServerInstance) NotifyDrpcReady(msg *srvpb.NotifyReadyReq) {
	srv.log.Debugf("%s instance %d drpc ready: %v", DataPlaneName, srv.Index(), msg)

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
func (srv *IOServerInstance) CallDrpc(method drpc.Method, body proto.Message) (*drpc.Response, error) {
	dc, err := srv.getDrpcClient()
	if err != nil {
		return nil, err
	}

	return makeDrpcCall(dc, method, body)
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
			errors.WithMessagef(err, "rank %s dRPC unmarshal failed", &rank),
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
		dresp, err := srv.CallDrpc(method, nil)
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

func (srv *IOServerInstance) getBioHealth(ctx context.Context, req *mgmtpb.BioHealthReq) (*mgmtpb.BioHealthResp, error) {
	dresp, err := srv.CallDrpc(drpc.MethodBioHealth, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.BioHealthResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal BioHealthQuery response")
	}

	if resp.Status != 0 {
		return nil, errors.Wrap(drpc.DaosStatus(resp.Status), "getBioHealth failed")
	}

	return resp, nil
}

func (srv *IOServerInstance) listSmdDevices(ctx context.Context, req *mgmtpb.SmdDevReq) (*mgmtpb.SmdDevResp, error) {
	dresp, err := srv.CallDrpc(drpc.MethodSmdDevs, req)
	if err != nil {
		return nil, err
	}

	resp := new(mgmtpb.SmdDevResp)
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal SmdListDevs response")
	}

	if resp.Status != 0 {
		return nil, errors.Wrap(drpc.DaosStatus(resp.Status), "listSmdDevices failed")
	}

	return resp, nil
}

// updateInUseBdevs updates-in-place the input list of controllers with
// new NVMe health stats and SMD metadata details.
//
// First map input controllers to their concatenated model+serial keys then
// retrieve metadata and health details for each SMD device (blobstore) on
// "this" IO server instance and update details in input controller list.
func (srv *IOServerInstance) updateInUseBdevs(ctx context.Context, ctrlrMap map[string]*storage.NvmeController) error {
	smdDevs, err := srv.listSmdDevices(ctx, new(mgmtpb.SmdDevReq))
	if err != nil {
		return errors.Wrapf(err, "instance %d listSmdDevices()", srv.Index())
	}

	for _, dev := range smdDevs.Devices {
		health, err := srv.getBioHealth(ctx, &mgmtpb.BioHealthReq{
			DevUuid: dev.Uuid,
		})
		if err != nil {
			return errors.Wrapf(err, "instance %d getBioHealth()", srv.Index())
		}

		modelSerial, emptyField, ok := checkModelSerial(health.Model, health.Serial)
		if !ok {
			srv.log.Debugf("instance %d: skipping health stats for uuid %s, %s id empty",
				srv.Index(), health.DevUuid, emptyField)
			continue
		}

		msg := fmt.Sprintf("instance %d: stats received for ctrlr model/serial %s from smd uuid %s",
			srv.Index(), modelSerial, dev.GetUuid())

		ctrlr, exists := ctrlrMap[modelSerial]
		if !exists {
			srv.log.Debug(msg + " didn't match any known controllers")
			continue
		}

		srv.log.Debugf("%s->%s", msg, ctrlr.PciAddr)

		// multiple updates for the same key expected when
		// more than one controller namespaces (and resident
		// blobstores) exist, stats will be the same for each
		if err := convert.Types(health, ctrlr.HealthStats); err != nil {
			return errors.Wrapf(err, "converting health for controller %s %s",
				modelSerial, ctrlr.PciAddr)
		}

		smdDev := new(storage.SmdDevice)
		if err := convert.Types(dev, smdDev); err != nil {
			return errors.Wrapf(err, "converting smd details for controller %s %s",
				modelSerial, ctrlr.PciAddr)
		}

		ctrlr.SmdDevices = append(ctrlr.SmdDevices, smdDev)
	}

	return nil
}
