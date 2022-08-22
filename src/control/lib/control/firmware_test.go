//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/proto"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/common/test"
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
		cmp.Comparer(test.CmpErrBool),
		cmp.Comparer(func(h1, h2 *hostlist.HostSet) bool {
			return h1.String() == h2.String()
		}),
	}
}

func TestControl_FirmwareQuery(t *testing.T) {
	scmPbResults, scmExpResults := getTestSCMQueryResults(t)
	nvmePbResults, nvmeExpResults := getTestNVMeQueryResults(t)

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
			req: &FirmwareQueryReq{SCM: true, NVMe: true},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &FirmwareQueryReq{SCM: true, NVMe: true},
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
					ScmResults: scmPbResults,
				}),
			},
			expResp: &FirmwareQueryResp{
				HostSCMFirmware: map[string][]*SCMQueryResult{
					"host1": scmExpResults,
				},
			},
		},
		"NVMe success": {
			req: &FirmwareQueryReq{NVMe: true},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil, &ctlpb.FirmwareQueryResp{
					NvmeResults: nvmePbResults,
				}),
			},
			expResp: &FirmwareQueryResp{
				HostNVMeFirmware: map[string][]*NVMeQueryResult{
					"host1": nvmeExpResults,
				},
			},
		},
		"both success": {
			req: &FirmwareQueryReq{SCM: true, NVMe: true},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil, &ctlpb.FirmwareQueryResp{
					ScmResults:  scmPbResults,
					NvmeResults: nvmePbResults,
				}),
			},
			expResp: &FirmwareQueryResp{
				HostSCMFirmware: map[string][]*SCMQueryResult{
					"host1": scmExpResults,
				},
				HostNVMeFirmware: map[string][]*NVMeQueryResult{
					"host1": nvmeExpResults,
				},
			},
		},
		"no NVMe on host": {
			req: &FirmwareQueryReq{SCM: true, NVMe: true},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil, &ctlpb.FirmwareQueryResp{
					ScmResults: scmPbResults,
				}),
			},
			expResp: &FirmwareQueryResp{
				HostSCMFirmware: map[string][]*SCMQueryResult{
					"host1": scmExpResults,
				},
				HostNVMeFirmware: map[string][]*NVMeQueryResult{
					"host1": {},
				},
			},
		},
		"no SCM on host": {
			req: &FirmwareQueryReq{SCM: true, NVMe: true},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil, &ctlpb.FirmwareQueryResp{
					NvmeResults: nvmePbResults,
				}),
			},
			expResp: &FirmwareQueryResp{
				HostSCMFirmware: map[string][]*SCMQueryResult{
					"host1": {},
				},
				HostNVMeFirmware: map[string][]*NVMeQueryResult{
					"host1": nvmeExpResults,
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mic := tc.mic
			if mic == nil {
				mic = DefaultMockInvokerConfig()
			}

			ctx := context.TODO()
			mi := NewMockInvoker(log, mic)

			gotResp, gotErr := FirmwareQuery(ctx, mi, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, getCmpOpts()...); diff != "" {
				t.Fatalf("Unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func getTestSCMQueryResults(t *testing.T) ([]*ctlpb.ScmFirmwareQueryResp, []*SCMQueryResult) {
	scmPbResults := []*ctlpb.ScmFirmwareQueryResp{
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

	scmExpResults := make([]*SCMQueryResult, 0, len(scmPbResults))
	for _, pbRes := range scmPbResults {
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

		scmExpResults = append(scmExpResults, res)
	}

	return scmPbResults, scmExpResults
}

func getTestNVMeQueryResults(t *testing.T) ([]*ctlpb.NvmeFirmwareQueryResp, []*NVMeQueryResult) {
	nvmeExpResults := []*NVMeQueryResult{
		{
			Device: *storage.MockNvmeController(1),
		},
		{
			Device: *storage.MockNvmeController(2),
		},
	}

	nvmePbResults := make([]*ctlpb.NvmeFirmwareQueryResp, 0, len(nvmeExpResults))
	for _, expRes := range nvmeExpResults {
		pb := new(proto.NvmeController)
		if err := pb.FromNative(&expRes.Device); err != nil {
			t.Fatalf("Failed to create PB NvmeController from native: %s", err)
		}

		pbRes := &ctlpb.NvmeFirmwareQueryResp{
			Device: pb.AsProto(),
		}

		nvmePbResults = append(nvmePbResults, pbRes)
	}

	return nvmePbResults, nvmeExpResults
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
		"unknown": {
			originalType: DeviceTypeUnknown,
			expPBType:    ctlpb.FirmwareUpdateReq_DeviceType(-1),
			expErr:       errors.New("invalid device type 0"),
		},
		"unrecognized": {
			originalType: DeviceType(12345),
			expPBType:    ctlpb.FirmwareUpdateReq_DeviceType(-1),
			expErr:       errors.New("invalid device type 12345"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := tc.originalType.toCtlPBType()

			test.CmpErr(t, tc.expErr, err)
			test.AssertEqual(t, tc.expPBType, result, "")
		})
	}
}

func TestControl_FirmwareUpdate(t *testing.T) {
	pbSCMResults, expSCMResults := getTestSCMUpdateResults(t)
	pbNVMeResults, expNVMeResults := getTestNVMeUpdateResults(t)

	for name, tc := range map[string]struct {
		mic     *MockInvokerConfig
		req     *FirmwareUpdateReq
		expResp *FirmwareUpdateResp
		expErr  error
	}{
		"unknown type": {
			req: &FirmwareUpdateReq{
				Type:         DeviceTypeUnknown,
				FirmwarePath: "/my/path",
			},
			expErr: errors.New("invalid device type 0"),
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
					ScmResults: pbSCMResults,
				}),
			},
			expResp: &FirmwareUpdateResp{
				HostSCMResult: map[string][]*SCMUpdateResult{
					"host1": expSCMResults,
				},
			},
		},
		"NVMe success": {
			req: &FirmwareUpdateReq{
				Type:         DeviceTypeNVMe,
				FirmwarePath: "/my/path",
			},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", nil, &ctlpb.FirmwareUpdateResp{
					NvmeResults: pbNVMeResults,
				}),
			},
			expResp: &FirmwareUpdateResp{
				HostNVMeResult: map[string][]*NVMeUpdateResult{
					"host1": expNVMeResults,
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mic := tc.mic
			if mic == nil {
				mic = DefaultMockInvokerConfig()
			}

			ctx := context.TODO()
			mi := NewMockInvoker(log, mic)

			gotResp, gotErr := FirmwareUpdate(ctx, mi, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, getCmpOpts()...); diff != "" {
				t.Fatalf("Unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func getTestSCMUpdateResults(t *testing.T) ([]*ctlpb.ScmFirmwareUpdateResp, []*SCMUpdateResult) {
	pbSCMResults := []*ctlpb.ScmFirmwareUpdateResp{
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

	expSCMResults := make([]*SCMUpdateResult, 0, len(pbSCMResults))
	for _, pbRes := range pbSCMResults {
		res := &SCMUpdateResult{}
		if err := convert.Types(pbRes.Module, &res.Module); err != nil {
			t.Fatalf("couldn't set up expected results: %v", err)
		}
		if pbRes.Error != "" {
			res.Error = errors.New(pbRes.Error)
		}

		expSCMResults = append(expSCMResults, res)
	}

	return pbSCMResults, expSCMResults
}

func getTestNVMeUpdateResults(t *testing.T) ([]*ctlpb.NvmeFirmwareUpdateResp, []*NVMeUpdateResult) {
	pbNVMeResults := []*ctlpb.NvmeFirmwareUpdateResp{
		{
			PciAddr: "TestDev1",
			Error:   "a surprising failure occurred",
		},
		{
			PciAddr: "TestDev2",
			Error:   "",
		},
	}

	expNVMeResults := make([]*NVMeUpdateResult, 0, len(pbNVMeResults))
	for _, pbRes := range pbNVMeResults {
		res := &NVMeUpdateResult{
			DevicePCIAddr: pbRes.PciAddr,
		}
		if pbRes.Error != "" {
			res.Error = errors.New(pbRes.Error)
		}

		expNVMeResults = append(expNVMeResults, res)
	}

	return pbNVMeResults, expNVMeResults
}
