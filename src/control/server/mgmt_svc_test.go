//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"net"
	"testing"

	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system/raft"
)

func TestServer_MgmtSvc_checkSystemRequest(t *testing.T) {
	for name, tc := range map[string]struct {
		req     proto.Message
		sysName string
		expErr  error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"wrong system": {
			req: &mgmtpb.SystemQueryReq{
				Sys: "wrong",
			},
			expErr: errors.New("does not match"),
		},
		"default system": {
			req: &mgmtpb.SystemQueryReq{
				Sys: build.DefaultSystemName,
			},
		},
		"versioned system": {
			req: &mgmtpb.SystemQueryReq{
				Sys: "system-1.2.3",
			},
			sysName: "system",
		},
		"hyphenated system name": {
			req: &mgmtpb.SystemQueryReq{
				Sys: "system-a-and-b",
			},
			sysName: "system-a-and-b",
		},
		"hyphenated, versioned system name": {
			req: &mgmtpb.SystemQueryReq{
				Sys: "system-a-and-b-1.2.3",
			},
			sysName: "system-a-and-b",
		},
		"version-like system name": {
			req: &mgmtpb.SystemQueryReq{
				Sys: "1.2.3",
			},
			sysName: "1.2.3",
		},
		"version-like system name with version": {
			req: &mgmtpb.SystemQueryReq{
				Sys: "1.2.3-3.2.1",
			},
			sysName: "1.2.3",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			svc := newTestMgmtSvc(t, log)
			if tc.sysName != "" {
				svc.sysdb = raft.MockDatabaseWithCfg(t, log, &raft.DatabaseConfig{
					SystemName: tc.sysName,
					Replicas:   []*net.TCPAddr{common.LocalhostCtrlAddr()},
				})
			}

			err := svc.checkSystemRequest(tc.req)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}
		})
	}
}
