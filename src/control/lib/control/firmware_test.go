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
// +build firmware

package control

import (
	"context"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func createTestHostSet(t *testing.T, hosts string) *hostlist.HostSet {
	set, err := hostlist.CreateSet(hosts)
	if err != nil {
		t.Fatalf("couldn't create host set: %s", err)
	}
	return set
}

func getCmpOpts() []cmp.Option {
	return []cmp.Option{
		cmp.Comparer(common.CmpErrBool),
		cmp.Comparer(func(h1, h2 hostlist.HostSet) bool {
			if h1.String() == h2.String() {
				return true
			}
			return false
		}),
	}
}

func TestControl_FirmwareQuery(t *testing.T) {
	pbResults := []*ctlpb.ScmFirmwareQueryResp{
		{
			Module: &ctlpb.ScmModule{
				Uid:             "TestUid1",
				Capacity:        (1 << 20),
				Physicalid:      1,
				Socketid:        2,
				Controllerid:    3,
				Channelid:       4,
				Channelposition: 5,
			},
			ActiveVersion:     "ACTIVE1",
			StagedVersion:     "STAGED",
			ImageMaxSizeBytes: 3200,
			UpdateStatus:      uint32(storage.ScmUpdateStatusStaged),
		},
		{
			Module: &ctlpb.ScmModule{
				Uid:             "TestUid2",
				Capacity:        (1 << 21),
				Physicalid:      6,
				Socketid:        7,
				Controllerid:    8,
				Channelid:       9,
				Channelposition: 10,
			},
			ActiveVersion:     "ACTIVE2",
			StagedVersion:     "",
			ImageMaxSizeBytes: 6400,
			UpdateStatus:      uint32(storage.ScmUpdateStatusSuccess),
		},
		{
			Module: &ctlpb.ScmModule{
				Uid:             "TestUid3",
				Capacity:        (1 << 22),
				Physicalid:      11,
				Socketid:        12,
				Controllerid:    13,
				Channelid:       14,
				Channelposition: 15,
			},
			Error: "Failed getting firmware info",
		},
	}

	expResults := make([]*SCMQueryResult, 0, len(pbResults))
	for _, pbRes := range pbResults {
		res := &SCMQueryResult{
			Module: storage.ScmModule{
				UID:             pbRes.Module.Uid,
				Capacity:        pbRes.Module.Capacity,
				PhysicalID:      pbRes.Module.Physicalid,
				SocketID:        pbRes.Module.Socketid,
				ControllerID:    pbRes.Module.Controllerid,
				ChannelID:       pbRes.Module.Channelid,
				ChannelPosition: pbRes.Module.Channelposition,
			},
			Info: &storage.ScmFirmwareInfo{
				ActiveVersion:     pbRes.ActiveVersion,
				StagedVersion:     pbRes.StagedVersion,
				ImageMaxSizeBytes: pbRes.ImageMaxSizeBytes,
				UpdateStatus:      storage.ScmFirmwareUpdateStatus(pbRes.UpdateStatus),
			},
		}

		if pbRes.Error != "" {
			res.Error = errors.New(pbRes.Error)
		}

		expResults = append(expResults, res)
	}

	for name, tc := range map[string]struct {
		mic     *MockInvokerConfig
		req     *FirmwareQueryReq
		expResp *FirmwareQueryResp
		expErr  error
	}{
		"nothing requested": {
			req:    &FirmwareQueryReq{},
			expErr: errors.New("no device types requested"),
		},
		"local failure": {
			req: &FirmwareQueryReq{SCM: true},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &FirmwareQueryReq{SCM: true},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expResp: &FirmwareQueryResp{
				HostErrorsResp: HostErrorsResp{
					HostErrors: HostErrorsMap{
						"remote failed": &HostErrorSet{
							HostSet:   createTestHostSet(t, "host1"),
							HostError: errors.New("remote failed"),
						},
					},
				},
			},
		},
		"SCM success": {
			req: &FirmwareQueryReq{SCM: true},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil, &ctlpb.FirmwareQueryResp{
					ScmResults: pbResults,
				}),
			},
			expResp: &FirmwareQueryResp{
				HostSCMFirmware: map[string][]*SCMQueryResult{
					"host1": expResults,
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mic := tc.mic
			if mic == nil {
				mic = DefaultMockInvokerConfig()
			}

			ctx := context.TODO()
			mi := NewMockInvoker(log, mic)

			gotResp, gotErr := FirmwareQuery(ctx, mi, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, getCmpOpts()...); diff != "" {
				t.Fatalf("Unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_DeviceType_toCtlPBType(t *testing.T) {
	for name, tc := range map[string]struct {
		originalType DeviceType
		expPBType    ctlpb.FirmwareUpdateReq_DeviceType
		expErr       error
	}{
		"SCM": {
			originalType: DeviceTypeSCM,
			expPBType:    ctlpb.FirmwareUpdateReq_SCM,
		},
		"NVMe": {
			originalType: DeviceTypeNVMe,
			expPBType:    ctlpb.FirmwareUpdateReq_NVMe,
		},
		"unrecognized": {
			originalType: DeviceType(12345),
			expPBType:    ctlpb.FirmwareUpdateReq_DeviceType(-1),
			expErr:       errors.New("invalid device type 12345"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := tc.originalType.toCtlPBType()

			common.CmpErr(t, tc.expErr, err)
			common.AssertEqual(t, tc.expPBType, result, "")
		})
	}
}

func TestControl_FirmwareUpdate(t *testing.T) {
	pbResults := []*ctlpb.ScmFirmwareUpdateResp{
		{
			Module: &ctlpb.ScmModule{
				Uid:             "TestUid1",
				Capacity:        (1 << 20),
				Physicalid:      1,
				Socketid:        2,
				Controllerid:    3,
				Channelid:       4,
				Channelposition: 5,
			},
			Error: "",
		},
		{
			Module: &ctlpb.ScmModule{
				Uid:             "TestUid2",
				Capacity:        (1 << 21),
				Physicalid:      6,
				Socketid:        7,
				Controllerid:    8,
				Channelid:       9,
				Channelposition: 10,
			},
			Error: "something went wrong",
		},
	}

	expResults := make([]*SCMUpdateResult, 0, len(pbResults))
	for _, pbRes := range pbResults {
		res := &SCMUpdateResult{}
		if err := convert.Types(pbRes.Module, &res.Module); err != nil {
			t.Fatalf("couldn't set up expected results: %v", err)
		}
		if pbRes.Error != "" {
			res.Error = errors.New(pbRes.Error)
		}

		expResults = append(expResults, res)
	}

	for name, tc := range map[string]struct {
		mic     *MockInvokerConfig
		req     *FirmwareUpdateReq
		expResp *FirmwareUpdateResp
		expErr  error
	}{
		"invalid type": {
			req: &FirmwareUpdateReq{
				Type:         DeviceType(5678),
				FirmwarePath: "/my/path",
			},
			expErr: errors.New("invalid device type 5678"),
		},
		"no path": {
			req: &FirmwareUpdateReq{
				Type: DeviceTypeSCM,
			},
			expErr: errors.New("firmware file path missing"),
		},
		"local failure": {
			req: &FirmwareUpdateReq{
				Type:         DeviceTypeSCM,
				FirmwarePath: "/my/path",
			},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &FirmwareUpdateReq{
				Type:         DeviceTypeSCM,
				FirmwarePath: "/my/path",
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expResp: &FirmwareUpdateResp{
				HostErrorsResp: HostErrorsResp{
					HostErrors: HostErrorsMap{
						"remote failed": &HostErrorSet{
							HostSet:   createTestHostSet(t, "host1"),
							HostError: errors.New("remote failed"),
						},
					},
				},
			},
		},
		"SCM success": {
			req: &FirmwareUpdateReq{
				Type:         DeviceTypeSCM,
				FirmwarePath: "/my/path",
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil, &ctlpb.FirmwareUpdateResp{
					ScmResults: pbResults,
				}),
			},
			expResp: &FirmwareUpdateResp{
				HostSCMResult: map[string][]*SCMUpdateResult{
					"host1": expResults,
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mic := tc.mic
			if mic == nil {
				mic = DefaultMockInvokerConfig()
			}

			ctx := context.TODO()
			mi := NewMockInvoker(log, mic)

			gotResp, gotErr := FirmwareUpdate(ctx, mi, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, getCmpOpts()...); diff != "" {
				t.Fatalf("Unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
