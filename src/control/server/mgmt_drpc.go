//
// (C) Copyright 2018-2019 Intel Corporation.
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

// #cgo CFLAGS: -I${SRCDIR}/../../../include
// #include <daos/drpc_modules.h>
import "C"

import (
	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"

	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/drpc"
)

const (
	mgmtModuleID  = C.DRPC_MODULE_MGMT
	killRank      = C.DRPC_METHOD_MGMT_KILL_RANK
	setRank       = C.DRPC_METHOD_MGMT_SET_RANK
	createMS      = C.DRPC_METHOD_MGMT_CREATE_MS
	startMS       = C.DRPC_METHOD_MGMT_START_MS
	join          = C.DRPC_METHOD_MGMT_JOIN
	getAttachInfo = C.DRPC_METHOD_MGMT_GET_ATTACH_INFO
	createPool    = C.DRPC_METHOD_MGMT_CREATE_POOL
	destroyPool   = C.DRPC_METHOD_MGMT_DESTROY_POOL
	bioHealth     = C.DRPC_METHOD_MGMT_BIO_HEALTH_QUERY
	setUp         = C.DRPC_METHOD_MGMT_SET_UP

	srvModuleID = C.DRPC_MODULE_SRV
	notifyReady = C.DRPC_METHOD_SRV_NOTIFY_READY
)

// mgmtModule is the management drpc module struct
// mgmtModule represents the daos_server mgmt dRPC module. It sends dRPCs to
// the daos_io_server iosrv module (src/iosrv).
type mgmtModule struct{}

// HandleCall is the handler for calls to the mgmtModule
func (m *mgmtModule) HandleCall(client *drpc.Client, method int32, body []byte) ([]byte, error) {
	return nil, errors.New("mgmt module handler is not implemented")
}

// InitModule is empty for this module
func (m *mgmtModule) InitModule(state drpc.ModuleState) {}

// ID will return Mgmt module ID
func (m *mgmtModule) ID() int32 {
	return mgmtModuleID
}

// srvModule represents the daos_server dRPC module. It handles dRPCs sent by
// the daos_io_server iosrv module (src/iosrv).
type srvModule struct {
	iosrv *iosrv
}

// HandleCall is the handler for calls to the srvModule
func (mod *srvModule) HandleCall(cli *drpc.Client, method int32, req []byte) ([]byte, error) {
	switch method {
	case notifyReady:
		return nil, mod.handleNotifyReady(req)
	default:
		return nil, errors.Errorf("unknown dRPC %d", method)
	}
}

func (mod *srvModule) InitModule(state drpc.ModuleState) {}

func (mod *srvModule) ID() int32 {
	return srvModuleID
}

func (mod *srvModule) handleNotifyReady(reqb []byte) error {
	req := &srvpb.NotifyReadyReq{}
	if err := proto.Unmarshal(reqb, req); err != nil {
		return errors.Wrap(err, "unmarshal NotifyReady request")
	}

	mod.iosrv.ready <- req

	return nil
}
