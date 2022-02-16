//
// (C) Copyright 2018-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"strings"

	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
	"github.com/daos-stack/daos/src/control/system/checker"
	"github.com/daos-stack/daos/src/control/system/raft"
)

// mgmtSvc implements (the Go portion of) Management Service, satisfying
// mgmtpb.MgmtSvcServer.
type mgmtSvc struct {
	mgmtpb.UnimplementedMgmtSvcServer
	log               logging.Logger
	harness           *EngineHarness
	membership        *system.Membership // if MS leader, system membership list
	sysdb             *raft.Database
	sysChecker        *checker.Coordinator
	rpcClient         control.UnaryInvoker
	events            *events.PubSub
	clientNetworkHint *mgmtpb.ClientNetHint
	joinReqs          joinReqChan
	groupUpdateReqs   chan bool
	lastMapVer        uint32
}

func newMgmtSvc(h *EngineHarness, m *system.Membership, s *raft.Database, c control.UnaryInvoker, p *events.PubSub) *mgmtSvc {
	return &mgmtSvc{
		log:               h.log,
		harness:           h,
		membership:        m,
		sysdb:             s,
		sysChecker:        checker.DefaultCoordinator(h.log, s, h),
		rpcClient:         c,
		events:            p,
		clientNetworkHint: new(mgmtpb.ClientNetHint),
		joinReqs:          make(joinReqChan),
		groupUpdateReqs:   make(chan bool),
	}
}

// checkSystemRequest sanity checks that a request is not nil and
// has been sent to the correct system.
func (svc *mgmtSvc) checkSystemRequest(req proto.Message) error {
	if common.InterfaceIsNil(req) {
		return errors.New("nil request")
	}
	if sReq, ok := req.(interface{ GetSys() string }); ok {
		comps := strings.Split(sReq.GetSys(), "-")
		sysName := comps[0]
		if len(comps) > 1 {
			if _, err := build.NewVersion(comps[len(comps)-1]); err == nil {
				sysName = strings.Join(comps[:len(comps)-1], "-")
			} else {
				sysName = strings.Join(comps, "-")
			}
		}

		if sysName != svc.sysdb.SystemName() {
			return FaultWrongSystem(sysName, svc.sysdb.SystemName())
		}
	}
	return nil
}

// checkLeaderRequest performs sanity-checking on a request that must
// be run on the current MS leader.
func (svc *mgmtSvc) checkLeaderRequest(req proto.Message) error {
	if err := svc.sysdb.CheckLeader(); err != nil {
		return err
	}
	return svc.checkSystemRequest(req)
}

// checkReplicaRequest performs sanity-checking on a request that must
// be run on a MS replica.
func (svc *mgmtSvc) checkReplicaRequest(req proto.Message) error {
	if err := svc.sysdb.CheckReplica(); err != nil {
		return err
	}
	return svc.checkSystemRequest(req)
}
