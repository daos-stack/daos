//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	"google.golang.org/grpc/credentials"
	"google.golang.org/grpc/peer"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
)

type testStatus struct {
	Status int32
}

func (ts *testStatus) GetStatus() int32 {
	return ts.Status
}

func TestServer_unaryStatusInterceptor(t *testing.T) {
	for name, tc := range map[string]struct {
		handlerResp interface{}
		handlerErr  error
		expErr      error
	}{
		"handler error": {
			handlerErr: errors.New("whoops"),
			expErr:     errors.New("whoops"),
		},
		"DAOS status 0": {
			handlerResp: &testStatus{},
		},
		"DAOS status -1005": {
			handlerResp: &testStatus{
				Status: -1005,
			},
			expErr: drpc.DaosNonexistant,
		},
		"non-status resp": {
			handlerResp: 42,
		},
	} {
		t.Run(name, func(t *testing.T) {
			handler := func(ctx context.Context, req interface{}) (interface{}, error) {
				return tc.handlerResp, tc.handlerErr
			}
			gotResp, gotErr := unaryStatusInterceptor(context.TODO(), nil, nil, handler)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.handlerResp, gotResp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

// newTestAuthCtx returns a context with a fake peer.PeerInfo
// set up to validate component access/versioning.
func newTestAuthCtx(parent context.Context, commonName string) context.Context {
	ctxPeer := &peer.Peer{
		Addr: common.LocalhostCtrlAddr(),
		AuthInfo: credentials.TLSInfo{
			State: tls.ConnectionState{
				VerifiedChains: [][]*x509.Certificate{
					{
						{
							Subject: pkix.Name{
								CommonName: commonName,
							},
						},
					},
				},
			},
		},
	}

	return peer.NewContext(parent, ctxPeer)
}

type checkVerReq struct {
	Sys string
}

func (r *checkVerReq) GetSys() string {
	return r.Sys
}

func TestServer_checkVersion(t *testing.T) {
	for name, tc := range map[string]struct {
		selfVersion  string
		otherVersion string
		ctx          context.Context
		nonSysMsg    bool
		expErr       error
	}{
		"unknown self version": {
			selfVersion: "0.0.0",
			expErr:      errors.New("self server version"),
		},
		"older other component (no version)": {
			selfVersion: "1.2.3",
			expErr:      errors.New("not compatible"),
		},
		"secure pre-2.0.0 component with version somehow is still incompatible": {
			selfVersion:  "2.2.0",
			otherVersion: "1.0.0",
			ctx:          newTestAuthCtx(context.TODO(), "agent"),
			expErr:       errors.New("not compatible"),
		},
		"unknown secure component rejected": {
			selfVersion:  "2.4.0",
			otherVersion: "2.4.0",
			ctx:          newTestAuthCtx(context.TODO(), "3v1l"),
			expErr:       errors.New("not compatible"),
		},
		"insecure versioned component defaults to server": {
			selfVersion:  "2.4.0",
			otherVersion: "2.4.1",
		},
		"secure versioned component": {
			selfVersion:  "2.4.0",
			otherVersion: "2.4.0",
			ctx:          newTestAuthCtx(context.TODO(), "agent"),
		},
		"non-sys msg bypasses version checks": {
			selfVersion: "2.4.0",
			nonSysMsg:   true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			ctx := context.TODO()
			if tc.ctx != nil {
				ctx = tc.ctx
			}
			selfComp := &build.VersionedComponent{
				Component: "server",
				Version:   build.MustNewVersion(tc.selfVersion),
			}

			var req interface{}
			if tc.nonSysMsg {
				req = struct{}{}
			} else {
				verReq := &checkVerReq{
					Sys: build.DefaultSystemName,
				}
				if tc.otherVersion != "" {
					verReq.Sys += "-" + tc.otherVersion
				}
				req = verReq
			}

			gotErr := checkVersion(ctx, selfComp, req)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}
