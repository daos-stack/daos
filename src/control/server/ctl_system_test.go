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
	"net"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/system"
)

const defaultAP = "192.168.1.1:10001"

func TestServer_CtlSvc_SystemStart(t *testing.T) {
	mockMember := system.NewMember(system.NilRank, "", &net.TCPAddr{}, system.MemberStateStopped)

	for name, tc := range map[string]struct {
		ranks      []uint32
		members    system.Members
		expResults []*ctlpb.RankResult
		//		hAddr      string
		clientResp *mgmtpb.RanksResp
		clientErr  error
		expErr     error
	}{
		"no memberss": {
			clientResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 0, Action: "start", State: uint32(system.MemberStateJoined)},
					{Rank: 1, Action: "start", State: uint32(system.MemberStateJoined)},
				},
			},
			expResults: []*ctlpb.RankResult{
				{Rank: 0, Action: "start", State: uint32(system.MemberStateJoined)},
				{Rank: 1, Action: "start", State: uint32(system.MemberStateJoined)},
			},
			expErr: errors.Wrap(system.FaultMemberMissing, "retrieving MS member: rank 1"),
		},
		"single rank": {
			members: system.Members{mockMember},
			clientResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 0, Action: "start", State: uint32(system.MemberStateJoined)},
					{Rank: 1, Action: "start", State: uint32(system.MemberStateJoined)},
				},
			},
			expResults: []*ctlpb.RankResult{
				{Rank: 0, Action: "start", State: uint32(system.MemberStateJoined)},
				{Rank: 1, Action: "start", State: uint32(system.MemberStateJoined)},
			},
			expErr: errors.Wrap(system.FaultMemberMissing, "retrieving MS member: rank 1"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			emptyCfg := emptyMockConfig(t)
			cs := mockControlService(t, log, emptyCfg, nil, nil, nil)
			cs.membership = system.NewMembership(log)

			ioserverCount := maxIOServers
			svc := newTestMgmtSvcMulti(log, ioserverCount, false)

			svc.harness.started.SetTrue()

			msClientCfg := mgmtSvcClientCfg{
				ControlAddr:  &net.TCPAddr{},
				AccessPoints: []string{defaultAP},
			}
			// create mock that implements MgmtSvcClient
			mockMSClient := proto.NewMockMgmtSvcClient(
				proto.MockMgmtSvcClientConfig{
					//					StartResp: tc.clientResp,
					//					StartErr:  tc.clientErr,
				})

			// store for checking calls later
			//mockMSClients[i] = mockMSClient.(*proto.MockMgmtSvcClient)
			mockConnectFn := func(ctx context.Context, ap string,
				tc *security.TransportConfig,
				fn func(context.Context, mgmtpb.MgmtSvcClient) error,
				extraDialOpts ...grpc.DialOption) error {

				return fn(ctx, mockMSClient)
			}
			// inject fn that uses the mock client to be used on connect

			for i, srv := range svc.harness.instances {
				srv._superblock.Rank = new(system.Rank)
				*srv._superblock.Rank = system.Rank(i + 1)
				if i == 0 {
					srv._superblock.MS = true
					msClient := newMgmtSvcClient(context.TODO(), log, msClientCfg)
					msClient.connectFn = mockConnectFn
				}
			}

			cs.harness = svc.harness
			cs.harnessClient = NewHarnessClient(log, svc.harness)

			gotResults, gotErr := cs.SystemStart(context.TODO(),
				&ctlpb.SystemStartReq{Ranks: tc.ranks})
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResults, gotResults, common.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected results (-want, +got)\n%s\n", diff)
			}
		})
	}
}
