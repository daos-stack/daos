//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/testing/protocmp"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
)

func defResCmpOpts() []cmp.Option {
	return []cmp.Option{
		cmp.Comparer(func(x, y *hostlist.HostSet) bool {
			return x.RangedString() == y.RangedString()
		}),
		cmpopts.IgnoreFields(HostErrorSet{}, "HostError"),
	}
}

func TestControl_HostErrorsMap(t *testing.T) {
	makeHosts := func(hosts ...string) []string {
		return hosts
	}
	makeErrors := func(errStrings ...string) (errs []error) {
		for _, errStr := range errStrings {
			errs = append(errs, errors.New(errStr))
		}
		return
	}

	for name, tc := range map[string]struct {
		hosts     []string
		errors    []error
		expErrMap HostErrorsMap
		expErr    error
	}{
		"nil host error": {
			hosts:     makeHosts("host1"),
			errors:    []error{nil},
			expErrMap: HostErrorsMap{},
		},
		"one host one error": {
			hosts:     makeHosts("host1"),
			errors:    makeErrors("whoops"),
			expErrMap: mockHostErrorsMap(t, &MockHostError{"host1", "whoops"}),
		},
		"two hosts one error": {
			hosts:  makeHosts("host1", "host2"),
			errors: makeErrors("whoops", "whoops"),
			expErrMap: mockHostErrorsMap(t,
				&MockHostError{"host1", "whoops"},
				&MockHostError{"host2", "whoops"},
			),
		},
		"two hosts two errors": {
			hosts:  makeHosts("host1", "host2"),
			errors: makeErrors("whoops", "oops"),
			expErrMap: mockHostErrorsMap(t,
				&MockHostError{"host1", "whoops"},
				&MockHostError{"host2", "oops"},
			),
		},
		"two hosts same port one error": {
			hosts:  makeHosts("host1:1", "host2:1"),
			errors: makeErrors("whoops", "whoops"),
			expErrMap: mockHostErrorsMap(t,
				&MockHostError{"host1:1", "whoops"},
				&MockHostError{"host2:1", "whoops"},
			),
		},
		"two hosts different port one error": {
			hosts:  makeHosts("host1:1", "host2:2"),
			errors: makeErrors("whoops", "whoops"),
			expErrMap: mockHostErrorsMap(t,
				&MockHostError{"host1:1", "whoops"},
				&MockHostError{"host2:2", "whoops"},
			),
		},
	} {
		t.Run(name, func(t *testing.T) {
			hem := make(HostErrorsMap)
			for i, host := range tc.hosts {
				gotErr := hem.Add(host, tc.errors[i])
				test.CmpErr(t, tc.expErr, gotErr)
				if tc.expErr != nil {
					return
				}
			}

			if diff := cmp.Diff(tc.expErrMap, hem, defResCmpOpts()...); diff != "" {
				t.Fatalf("unexpected map (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_getMSResponse(t *testing.T) {
	for name, tc := range map[string]struct {
		resp    *UnaryResponse
		expResp proto.Message
		expErr  error
	}{
		"nil response": {
			expErr: errors.New("nil *control.UnaryResponse"),
		},
		"not MS response": {
			resp:   &UnaryResponse{},
			expErr: errors.New("did not come"),
		},
		"empty responses": {
			resp: &UnaryResponse{
				fromMS: true,
			},
			expErr: errNoMsResponse,
		},
		"nil host response": {
			resp: &UnaryResponse{
				fromMS: true,
				Responses: []*HostResponse{
					nil,
				},
			},
			expErr: errNoMsResponse,
		},
		"nil response message": {
			resp:   MockMSResponse("host1", nil, nil),
			expErr: errors.New("message was nil"),
		},
		"mixed versioned and non-versioned responses": {
			// NB: This is a weird "can't happen" situation, but
			// it shouldn't cause a panic.
			resp: &UnaryResponse{
				fromMS: true,
				Responses: []*HostResponse{
					{
						Addr: "host1",
						Message: &mgmtpb.LeaderQueryResp{
							Replicas: []string{"host1", "host2"},
						},
					},
					{
						Addr: "host2",
						Message: &mgmtpb.SystemQueryResp{
							DataVersion: 1,
						},
					},
				},
			},
			expResp: &mgmtpb.SystemQueryResp{
				DataVersion: 1,
			},
		},
		"valid non-versioned response picked": {
			resp: &UnaryResponse{
				fromMS: true,
				Responses: []*HostResponse{
					{
						Addr:  "host1",
						Error: errors.New("whoops"),
					},
					{
						Addr: "host2",
						Message: &mgmtpb.LeaderQueryResp{
							Replicas: []string{"host3", "host1"},
						},
					},
					{
						Addr: "host3",
						Message: &mgmtpb.LeaderQueryResp{
							Replicas: []string{"host2", "host1"},
						},
					},
				},
			},
			expResp: &mgmtpb.LeaderQueryResp{
				// Expect first valid response
				Replicas: []string{"host3", "host1"},
			},
		},
		"valid versioned response picked": {
			resp: &UnaryResponse{
				fromMS: true,
				Responses: []*HostResponse{
					{
						Addr:  "host1",
						Error: errors.New("whoops"),
					},
					{
						Addr: "host2",
						Message: &mgmtpb.SystemQueryResp{
							DataVersion: 1,
						},
					},
				},
			},
			expResp: &mgmtpb.SystemQueryResp{
				DataVersion: 1,
			},
		},
		"highest data version returned": {
			resp: &UnaryResponse{
				fromMS: true,
				Responses: []*HostResponse{
					{
						Addr: "host1",
						Message: &mgmtpb.SystemQueryResp{
							DataVersion: 3,
						},
					},
					{
						Addr: "host2",
						Message: &mgmtpb.SystemQueryResp{
							DataVersion: 5,
						},
					},
					{
						Addr: "host3",
						Message: &mgmtpb.SystemQueryResp{
							DataVersion: 4,
						},
					},
					{
						Addr: "host4",
						Message: &mgmtpb.SystemQueryResp{
							DataVersion: 3,
						},
					},
				},
			},
			expResp: &mgmtpb.SystemQueryResp{
				DataVersion: 5,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotResp, gotErr := tc.resp.getMSResponse()
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := []cmp.Option{
				protocmp.Transform(),
			}
			if diff := cmp.Diff(tc.expResp, gotResp, cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
