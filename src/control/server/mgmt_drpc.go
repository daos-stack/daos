//
// (C) Copyright 2018-2020 Intel Corporation.
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
	"github.com/golang/protobuf/proto"
	"github.com/google/uuid"
	"github.com/pkg/errors"

	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

// mgmtModule represents the daos_server mgmt dRPC module. It sends dRPCs to
// the daos_io_server iosrv module (src/iosrv).
type mgmtModule struct{}

// HandleCall is the handler for calls to the mgmtModule
func (m *mgmtModule) HandleCall(session *drpc.Session, method drpc.Method, req []byte) ([]byte, error) {
	return nil, drpc.UnknownMethodFailure()
}

// ID will return Mgmt module ID
func (m *mgmtModule) ID() drpc.ModuleID {
	return drpc.ModuleMgmt
}

// srvModule represents the daos_server dRPC module. It handles dRPCs sent by
// the daos_io_server iosrv module (src/iosrv).
type srvModule struct {
	log    logging.Logger
	sysdb  *system.Database
	iosrvs []*IOServerInstance
}

// HandleCall is the handler for calls to the srvModule.
func (mod *srvModule) HandleCall(session *drpc.Session, method drpc.Method, req []byte) ([]byte, error) {
	switch method {
	case drpc.MethodNotifyReady:
		return nil, mod.handleNotifyReady(req)
	case drpc.MethodBIOError:
		return nil, mod.handleBioErr(req)
	case drpc.MethodGetPoolServiceRanks:
		return mod.handleGetPoolServiceRanks(req)
	case drpc.MethodPoolCreateUpcall:
		return nil, mod.handlePoolCreateUpcall(req)
	case drpc.MethodPoolDestroyUpcall:
		return nil, mod.handlePoolDestroyUpcall(req)
	case drpc.MethodPoolListUpcall:
		return mod.handlePoolListUpcall(req)
	default:
		return nil, drpc.UnknownMethodFailure()
	}
}

func (mod *srvModule) ID() drpc.ModuleID {
	return drpc.ModuleSrv
}

func (mod *srvModule) handlePoolListUpcall(reqb []byte) ([]byte, error) {
	req := new(srvpb.PoolListUpcall)
	if err := proto.Unmarshal(reqb, req); err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}

	mod.log.Debugf("handling PoolListUpcall: %+v", req)

	resp := new(srvpb.PoolListUpcallResp)
	for i, ps := range mod.sysdb.PoolServiceList() {
		resp.Pools = append(resp.Pools, &srvpb.PoolListUpcallResp_Pool{
			Uuid:    ps.PoolUUID.String(),
			Svcreps: system.RanksToUint32(ps.Replicas),
		})
		if uint64(i) == req.Npools {
			break
		}
	}

	mod.log.Debugf("PoolListUpcallResp: %+v", resp)

	return proto.Marshal(resp)
}

func (mod *srvModule) handlePoolCreateUpcall(reqb []byte) error {
	req := new(srvpb.PoolCreateUpcall)
	if err := proto.Unmarshal(reqb, req); err != nil {
		return drpc.UnmarshalingPayloadFailure()
	}

	uuid, err := uuid.Parse(req.GetUuid())
	if err != nil {
		return errors.Wrapf(err, "invalid pool uuid %q", uuid)
	}

	mod.log.Debugf("handling PoolCreateUpcall: %+v", req)

	ps, err := mod.sysdb.FindPoolServiceByUUID(uuid)
	if err != nil && !system.IsFindPoolError(err) {
		return err
	}

	if ps != nil {
		return drpc.DaosAlready
	}

	ps = &system.PoolService{
		PoolUUID: uuid,
		Replicas: system.RanksFromUint32(req.GetSvcreps()),
	}
	return mod.sysdb.AddPoolService(ps)
}

func (mod *srvModule) handlePoolDestroyUpcall(reqb []byte) error {
	req := new(srvpb.PoolDestroyUpcall)
	if err := proto.Unmarshal(reqb, req); err != nil {
		return drpc.UnmarshalingPayloadFailure()
	}

	uuid, err := uuid.Parse(req.GetUuid())
	if err != nil {
		return errors.Wrapf(err, "invalid pool uuid %q", uuid)
	}

	mod.log.Debugf("handling PoolDestroyUpcall: %+v", req)

	ps, err := mod.sysdb.FindPoolServiceByUUID(uuid)
	if err != nil && !system.IsFindPoolError(err) {
		return err
	}

	if ps == nil {
		return drpc.DaosAlready
	}

	return mod.sysdb.RemovePoolService(uuid)
}

func (mod *srvModule) handleGetPoolServiceRanks(reqb []byte) ([]byte, error) {
	req := new(srvpb.GetPoolSvcReq)
	if err := proto.Unmarshal(reqb, req); err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}

	uuid, err := uuid.Parse(req.GetUuid())
	if err != nil {
		return nil, errors.Wrapf(err, "invalid pool uuid %q", uuid)
	}

	mod.log.Debugf("handling GetPoolSvcReq: %+v", req)

	ps, err := mod.sysdb.FindPoolServiceByUUID(uuid)
	if err != nil {
		return nil, err
	}

	resp := new(srvpb.GetPoolSvcResp)
	resp.Svcreps = system.RanksToUint32(ps.Replicas)

	mod.log.Debugf("GetPoolSvcResp: %+v", resp)

	return proto.Marshal(resp)
}

func (mod *srvModule) handleNotifyReady(reqb []byte) error {
	req := &srvpb.NotifyReadyReq{}
	if err := proto.Unmarshal(reqb, req); err != nil {
		return drpc.UnmarshalingPayloadFailure()
	}

	if req.InstanceIdx >= uint32(len(mod.iosrvs)) {
		return errors.Errorf("instance index %v is out of range (%v instances)",
			req.InstanceIdx, len(mod.iosrvs))
	}

	if err := checkDrpcClientSocketPath(req.DrpcListenerSock); err != nil {
		return errors.Wrap(err, "check NotifyReady request socket path")
	}

	mod.iosrvs[req.InstanceIdx].NotifyDrpcReady(req)

	return nil
}

func (mod *srvModule) handleBioErr(reqb []byte) error {
	req := &srvpb.BioErrorReq{}
	if err := proto.Unmarshal(reqb, req); err != nil {
		return errors.Wrap(err, "unmarshal BioError request")
	}

	if req.InstanceIdx >= uint32(len(mod.iosrvs)) {
		return errors.Errorf("instance index %v is out of range (%v instances)",
			req.InstanceIdx, len(mod.iosrvs))
	}

	if err := checkDrpcClientSocketPath(req.DrpcListenerSock); err != nil {
		return errors.Wrap(err, "check BioErr request socket path")
	}

	mod.iosrvs[req.InstanceIdx].BioErrorNotify(req)

	return nil
}
