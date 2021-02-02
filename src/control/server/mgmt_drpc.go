//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"github.com/golang/protobuf/proto"
	"github.com/google/uuid"
	"github.com/pkg/errors"

	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

// mgmtModule represents the daos_server mgmt dRPC module. It sends dRPCs to
// the daos_io_server iosrv module (src/iosrv) but doesn't receive.
type mgmtModule struct{}

// newMgmtModule creates a new management module and returns its reference.
func newMgmtModule() *mgmtModule {
	return &mgmtModule{}
}

// HandleCall is the handler for calls to the mgmtModule
func (mod *mgmtModule) HandleCall(session *drpc.Session, method drpc.Method, req []byte) ([]byte, error) {
	return nil, drpc.UnknownMethodFailure()
}

// ID will return Mgmt module ID
func (mod *mgmtModule) ID() drpc.ModuleID {
	return drpc.ModuleMgmt
}

// srvModule represents the daos_server dRPC module. It handles dRPCs sent by
// the daos_io_server iosrv module (src/iosrv).
type srvModule struct {
	log    logging.Logger
	sysdb  *system.Database
	iosrvs []*IOServerInstance
	events *events.PubSub
}

// newSrvModule creates a new srv module references to the system database,
// resident IOServerInstances and event publish subscribe reference.
func newSrvModule(log logging.Logger, sysdb *system.Database, iosrvs []*IOServerInstance, events *events.PubSub) *srvModule {
	return &srvModule{
		log:    log,
		sysdb:  sysdb,
		iosrvs: iosrvs,
		events: events,
	}
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
	case drpc.MethodClusterEvent:
		return mod.handleClusterEvent(req)
	default:
		return nil, drpc.UnknownMethodFailure()
	}
}

// ID will return SRV module ID
func (mod *srvModule) ID() drpc.ModuleID {
	return drpc.ModuleSrv
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

	resp := new(srvpb.GetPoolSvcResp)

	ps, err := mod.sysdb.FindPoolServiceByUUID(uuid)
	if err != nil {
		resp.Status = int32(drpc.DaosNonexistant)
		mod.log.Debugf("GetPoolSvcResp: %+v", resp)
		return proto.Marshal(resp)
		// return nil, err
	}

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

func (mod *srvModule) handleClusterEvent(reqb []byte) ([]byte, error) {
	req := new(sharedpb.ClusterEventReq)
	if err := proto.Unmarshal(reqb, req); err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}

	resp, err := mod.events.HandleClusterEvent(req)
	if err != nil {
		return nil, errors.Wrapf(err, "handle cluster event %+v", req)
	}

	return proto.Marshal(resp)
}
