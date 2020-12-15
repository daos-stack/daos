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
	"context"
	"testing"

	"github.com/golang/protobuf/proto"
	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/system"
)

func TestServer_MgmtSvc_GetAttachInfo(t *testing.T) {
	for name, tc := range map[string]struct {
		mgmtSvc          *mgmtSvc
		clientNetworkCfg *config.ClientNetworkCfg
		req              *mgmtpb.GetAttachInfoReq
		expResp          *mgmtpb.GetAttachInfoResp
	}{
		"Server uses verbs + Infiniband": {
			clientNetworkCfg: &config.ClientNetworkCfg{Provider: "ofi+verbs", CrtCtxShareAddr: 1, CrtTimeout: 10, NetDevClass: netdetect.Infiniband},
			req:              &mgmtpb.GetAttachInfoReq{},
			expResp:          &mgmtpb.GetAttachInfoResp{Provider: "ofi+verbs", CrtCtxShareAddr: 1, CrtTimeout: 10, NetDevClass: netdetect.Infiniband},
		},
		"Server uses sockets + Ethernet": {
			clientNetworkCfg: &config.ClientNetworkCfg{Provider: "ofi+sockets", CrtCtxShareAddr: 0, CrtTimeout: 5, NetDevClass: netdetect.Ether},
			req:              &mgmtpb.GetAttachInfoReq{},
			expResp:          &mgmtpb.GetAttachInfoResp{Provider: "ofi+sockets", CrtCtxShareAddr: 0, CrtTimeout: 5, NetDevClass: netdetect.Ether},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)
			harness := NewIOServerHarness(log)
			srv := newTestIOServer(log, true)

			if err := harness.AddInstance(srv); err != nil {
				t.Fatal(err)
			}
			srv.setDrpcClient(newMockDrpcClient(nil))
			harness.started.SetTrue()

			cfg := new(mockDrpcClientConfig)
			rb, _ := proto.Marshal(&mgmtpb.GetAttachInfoResp{})
			cfg.setSendMsgResponse(drpc.Status_SUCCESS, rb, nil)
			srv.setDrpcClient(newMockDrpcClient(cfg))
			tc.mgmtSvc = newMgmtSvc(harness, nil, system.MockDatabase(t, log))
			tc.mgmtSvc.clientNetworkCfg = tc.clientNetworkCfg
			gotResp, gotErr := tc.mgmtSvc.GetAttachInfo(context.TODO(), tc.req)
			if gotErr != nil {
				t.Fatalf("unexpected error: %+v\n", gotErr)
			}

			if diff := cmp.Diff(tc.expResp, gotResp); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}
