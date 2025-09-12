//
// (C) Copyright 2018-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"

	"github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
	"github.com/daos-stack/daos/src/control/system/checker"
	"github.com/daos-stack/daos/src/control/system/raft"
)

// mgmtModule represents the daos_server mgmt dRPC module. It sends dRPCs to
// the daos_engine (src/engine) but doesn't receive.
type mgmtModule struct{}

// newMgmtModule creates a new management module and returns its reference.
func newMgmtModule() *mgmtModule {
	return &mgmtModule{}
}

// GetMethod always errors because this module sends only, and doesn't implement any dRPC methods.
func (mod *mgmtModule) GetMethod(id int32) (drpc.Method, error) {
	return nil, errors.New("mgmtModule implements no methods")
}

// HandleCall is the handler for calls to the mgmtModule
func (mod *mgmtModule) HandleCall(ctx context.Context, session *drpc.Session, method drpc.Method, req []byte) ([]byte, error) {
	logging.FromContext(ctx).Errorf("attempted to call HandleCall on mgmtModule, which services no methods")
	return nil, drpc.UnknownMethodFailure()
}

// ID will return Mgmt module ID
func (mod *mgmtModule) ID() int32 {
	return daos.ModuleMgmt
}

func (mod *mgmtModule) String() string {
	return "server_mgmt"
}

// poolDatabase defines an interface to be implemented by
// a system pool database.
type poolDatabase interface {
	FindPoolServiceByLabel(string) (*system.PoolService, error)
	FindPoolServiceByUUID(uuid.UUID) (*system.PoolService, error)
	PoolServiceList(bool) ([]*system.PoolService, error)
	AddPoolService(context.Context, *system.PoolService) error
	RemovePoolService(context.Context, uuid.UUID) error
	UpdatePoolService(context.Context, *system.PoolService) error
	TakePoolLock(context.Context, uuid.UUID) (*raft.PoolLock, error)
}

// srvModule represents the daos_server dRPC module. It handles dRPCs sent by
// the daos_engine (src/engine).
type srvModule struct {
	log        logging.Logger
	poolDB     poolDatabase
	checkerDB  checker.FindingStore
	engines    []Engine
	events     *events.PubSub
	client     *control.Client
	msReplicas []string
}

// newSrvModule creates a new srv module references to the system database,
// resident EngineInstances and event publish subscribe reference.
func newSrvModule(log logging.Logger, pdb poolDatabase, cdb checker.FindingStore, engines []Engine, events *events.PubSub, client *control.Client, msReplicas []string) *srvModule {
	return &srvModule{
		log:        log,
		poolDB:     pdb,
		checkerDB:  cdb,
		engines:    engines,
		events:     events,
		client:     client,
		msReplicas: msReplicas,
	}
}

// GetMethod gets the corresponding Method for the method ID.
func (mod *srvModule) GetMethod(id int32) (drpc.Method, error) {
	switch id {
	case daos.MethodNotifyReady.ID(),
		daos.MethodGetPoolServiceRanks.ID(),
		daos.MethodPoolFindByLabel.ID(),
		daos.MethodClusterEvent.ID(),
		daos.MethodCheckerListPools.ID(),
		daos.MethodCheckerRegisterPool.ID(),
		daos.MethodCheckerDeregisterPool.ID(),
		daos.MethodCheckerReport.ID(),
		daos.MethodListPools.ID(),
		daos.MethodGetProps.ID():
		return daos.SrvMethod(id), nil
	default:
		return nil, fmt.Errorf("invalid method %d for module %s", id, mod.String())
	}
}

func (mod *srvModule) String() string {
	return "server"
}

// HandleCall is the handler for calls to the srvModule.
func (mod *srvModule) HandleCall(ctx context.Context, session *drpc.Session, method drpc.Method, req []byte) (_ []byte, err error) {
	defer func() {
		msg := ": success"
		if err != nil {
			msg = ", failed: " + err.Error()
		}
		mod.log.Tracef("srv upcall: %s%s", method, msg)
	}()

	switch method {
	case daos.MethodNotifyReady:
		return nil, mod.handleNotifyReady(req)
	case daos.MethodGetPoolServiceRanks:
		return mod.handleGetPoolServiceRanks(req)
	case daos.MethodPoolFindByLabel:
		return mod.handlePoolFindByLabel(req)
	case daos.MethodClusterEvent:
		return mod.handleClusterEvent(req)
	case daos.MethodCheckerListPools:
		return mod.handleCheckerListPools(ctx, req)
	case daos.MethodCheckerRegisterPool:
		return mod.handleCheckerRegisterPool(ctx, req)
	case daos.MethodCheckerDeregisterPool:
		return mod.handleCheckerDeregisterPool(ctx, req)
	case daos.MethodCheckerReport:
		return mod.handleCheckerReport(ctx, req)
	case daos.MethodListPools:
		return mod.handleListPools(req)
	case daos.MethodGetProps:
		return mod.handleGetProps(req)
	default:
		return nil, drpc.UnknownMethodFailure()
	}
}

// ID will return SRV module ID
func (mod *srvModule) ID() int32 {
	return daos.ModuleSrv
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

	ps, err := mod.poolDB.FindPoolServiceByUUID(uuid)
	if err != nil || ps.State != system.PoolServiceStateReady {
		resp.Status = int32(daos.Nonexistent)
		mod.log.Debugf("GetPoolSvcResp: %+v", resp)
		return proto.Marshal(resp)
	}

	resp.Svcreps = ranklist.RanksToUint32(ps.Replicas)

	mod.log.Debugf("GetPoolSvcResp: %+v", resp)

	return proto.Marshal(resp)
}

func (mod *srvModule) handlePoolFindByLabel(reqb []byte) ([]byte, error) {
	req := new(srvpb.PoolFindByLabelReq)
	if err := proto.Unmarshal(reqb, req); err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}

	mod.log.Debugf("handling PoolFindByLabel: %+v", req)

	resp := new(srvpb.PoolFindByLabelResp)

	ps, err := mod.poolDB.FindPoolServiceByLabel(req.GetLabel())
	if err != nil || ps.State != system.PoolServiceStateReady {
		resp.Status = int32(daos.Nonexistent)
		mod.log.Debugf("PoolFindByLabelResp: %+v", resp)
		return proto.Marshal(resp)
	}

	resp.Svcreps = ranklist.RanksToUint32(ps.Replicas)
	resp.Uuid = ps.PoolUUID.String()
	mod.log.Debugf("GetPoolSvcResp: %+v", resp)

	return proto.Marshal(resp)
}

func (mod *srvModule) handleNotifyReady(reqb []byte) error {
	req := &srvpb.NotifyReadyReq{}
	if err := proto.Unmarshal(reqb, req); err != nil {
		return drpc.UnmarshalingPayloadFailure()
	}

	if req.InstanceIdx >= uint32(len(mod.engines)) {
		return errors.Errorf("instance index %v is out of range (%v instances)",
			req.InstanceIdx, len(mod.engines))
	}

	if err := checkDrpcClientSocketPath(req.DrpcListenerSock); err != nil {
		return errors.Wrap(err, "check NotifyReady request socket path")
	}

	mod.engines[req.InstanceIdx].NotifyDrpcReady(req)

	return nil
}

func (mod *srvModule) handleClusterEvent(reqb []byte) ([]byte, error) {
	req := new(sharedpb.ClusterEventReq)
	if err := proto.Unmarshal(reqb, req); err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}

	resp, err := mod.events.HandleClusterEvent(req, false)
	if err != nil {
		return nil, errors.Wrapf(err, "handle cluster event %+v", req)
	}

	return proto.Marshal(resp)
}

func (mod *srvModule) handleListPools(reqb []byte) ([]byte, error) {
	req := new(srvpb.ListPoolsReq)
	if err := proto.Unmarshal(reqb, req); err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}

	mod.log.Tracef("%T: %+v", req, req)
	pools, err := mod.poolDB.PoolServiceList(req.IncludeAll)
	if err != nil {
		return nil, errors.Wrap(err, "failed to list system pools")
	}

	resp := new(srvpb.ListPoolsResp)
	resp.Pools = make([]*srvpb.ListPoolsResp_Pool, len(pools))
	for i, ps := range pools {
		resp.Pools[i] = &srvpb.ListPoolsResp_Pool{
			Uuid:    ps.PoolUUID.String(),
			Label:   ps.PoolLabel,
			Svcreps: ranklist.RanksToUint32(ps.Replicas),
		}
	}
	mod.log.Tracef("%T %+v", resp, resp)

	return proto.Marshal(resp)
}

func (mod *srvModule) handleGetProps(reqb []byte) ([]byte, error) {
	req := new(mgmtpb.SystemGetPropReq)
	if err := proto.Unmarshal(reqb, req); err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}

	mod.log.Tracef("%T: %+v", req, req)

	ms_req := &control.SystemGetPropReq{
		Keys: make([]daos.SystemPropertyKey, 0, len(req.Keys)),
	}
	for _, k := range req.Keys {
		var t daos.SystemPropertyKey
		err := t.FromString(k)
		if err != nil {
			return nil, errors.Wrapf(err, "invalid system property key %q", k)
		}
		ms_req.Keys = append(ms_req.Keys, t)
	}
	ms_req.SetHostList(mod.msReplicas)

	ms_resp, err := control.SystemGetProp(context.TODO(), mod.client, ms_req)
	if err != nil {
		return nil, errors.Wrap(err, "failed to get system properties from MS")
	}

	resp := &mgmtpb.SystemGetPropResp{
		Properties: make(map[string]string, len(ms_resp.Properties)),
	}
	for _, p := range ms_resp.Properties {
		resp.Properties[p.Key.String()] = p.Value.String()
	}

	return proto.Marshal(resp)
}
