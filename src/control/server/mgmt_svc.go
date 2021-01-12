//
// (C) Copyright 2018-2021 Intel Corporation.
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
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/system"
)

// mgmtSvc implements (the Go portion of) Management Service, satisfying
// mgmtpb.MgmtSvcServer.
type mgmtSvc struct {
	log              logging.Logger
	harness          *IOServerHarness
	membership       *system.Membership // if MS leader, system membership list
	sysdb            *system.Database
	rpcClient        control.UnaryInvoker
	events           *events.PubSub
	clientNetworkCfg *config.ClientNetworkCfg
	joinReqs         joinReqChan
}

func newMgmtSvc(h *IOServerHarness, m *system.Membership, s *system.Database, c control.UnaryInvoker, p *events.PubSub) *mgmtSvc {
	return &mgmtSvc{
		log:              h.log,
		harness:          h,
		membership:       m,
		sysdb:            s,
		rpcClient:        c,
		events:           p,
		clientNetworkCfg: new(config.ClientNetworkCfg),
		joinReqs:         make(joinReqChan),
	}
}

// checkSystemRequest sanity checks that a request is not nil and
// has been sent to the correct system.
func (svc *mgmtSvc) checkSystemRequest(req proto.Message) error {
	if common.InterfaceIsNil(req) {
		return errors.New("nil request")
	}
	if sReq, ok := req.(interface{ GetSys() string }); ok {
		if sReq.GetSys() != svc.sysdb.SystemName() {
			return FaultWrongSystem(sReq.GetSys(), svc.sysdb.SystemName())
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
