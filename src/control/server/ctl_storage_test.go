//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

func TestServer_CtlSvc_getScmUsage(t *testing.T) {
	mockScmNs0 := storage.MockScmNamespace(0)
	mockScmMountPath0 := "/mnt/daos0"
	mockScmNs0wMount := storage.MockScmNamespace(0)
	mockScmNs0wMount.Mount = storage.MockScmMountPoint(0)
	mockScmNs1 := storage.MockScmNamespace(1)
	mockScmMountPath1 := "/mnt/daos1"
	mockScmNs1wMount := storage.MockScmNamespace(1)
	mockScmNs1wMount.Mount = storage.MockScmMountPoint(1)

	for name, tc := range map[string]struct {
		smsc        *scm.MockSysConfig
		inResp      *storage.ScmScanResponse
		storageCfgs []storage.TierConfigs
		expErr      error
		expOutResp  *storage.ScmScanResponse
	}{
		"nil input response": {
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()).
						WithScmMountPoint(mockScmMountPath0).
						WithScmDeviceList(mockScmNs0.BlockDevice),
				},
			},
			expErr: errors.New("response is nil"),
		},
		"no scm tier": {
			inResp: new(storage.ScmScanResponse),
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithScmMountPoint(mockScmMountPath0).
						WithScmDeviceList(mockScmNs0.BlockDevice),
				},
			},
			expErr: errors.New("expected exactly 1 SCM tier"),
		},
		"no namespaces in input response": {
			inResp: new(storage.ScmScanResponse),
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()).
						WithScmMountPoint(mockScmMountPath0).
						WithScmDeviceList(mockScmNs0.BlockDevice),
				},
			},
			expErr: errors.New("missing namespaces"),
		},
		"no namespace matching pmem blockdevice in scan response": {
			inResp: &storage.ScmScanResponse{
				Namespaces: storage.ScmNamespaces{},
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()).
						WithScmMountPoint(mockScmMountPath0).
						WithScmDeviceList(mockScmNs0.BlockDevice),
				},
			},
			expErr: errors.New("no pmem namespace"),
		},
		"get usage fails": {
			smsc: &scm.MockSysConfig{
				GetfsUsageResps: []scm.GetfsUsageRetval{
					{Err: errors.New("unknown")},
				},
			},
			inResp: &storage.ScmScanResponse{
				Namespaces: storage.ScmNamespaces{
					mockScmNs0,
				},
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()).
						WithScmMountPoint(mockScmMountPath0).
						WithScmDeviceList(mockScmNs0.BlockDevice),
				},
			},
			expErr: errors.New("unknown"),
		},
		"get usage": {
			smsc: &scm.MockSysConfig{
				GetfsUsageResps: []scm.GetfsUsageRetval{
					{
						Total: mockScmNs0wMount.Mount.TotalBytes,
						Avail: mockScmNs0wMount.Mount.AvailBytes,
					},
				},
			},
			inResp: &storage.ScmScanResponse{
				Namespaces: storage.ScmNamespaces{
					mockScmNs0,
				},
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()).
						WithScmMountPoint(mockScmMountPath0).
						WithScmDeviceList(mockScmNs0.BlockDevice),
				},
			},
			expOutResp: &storage.ScmScanResponse{
				Namespaces: storage.ScmNamespaces{
					mockScmNs0wMount,
				},
			},
		},
		"get usage; multiple engines": {
			smsc: &scm.MockSysConfig{
				GetfsUsageResps: []scm.GetfsUsageRetval{
					{
						Total: mockScmNs0wMount.Mount.TotalBytes,
						Avail: mockScmNs0wMount.Mount.AvailBytes,
					},
					{
						Total: mockScmNs1wMount.Mount.TotalBytes,
						Avail: mockScmNs1wMount.Mount.AvailBytes,
					},
				},
			},
			inResp: &storage.ScmScanResponse{
				Namespaces: storage.ScmNamespaces{
					mockScmNs0,
					mockScmNs1,
				},
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()).
						WithScmMountPoint(mockScmMountPath0).
						WithScmDeviceList(mockScmNs0.BlockDevice),
				},
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()).
						WithScmMountPoint(mockScmMountPath1).
						WithScmDeviceList(mockScmNs1.BlockDevice),
				},
			},
			expOutResp: &storage.ScmScanResponse{
				Namespaces: storage.ScmNamespaces{
					mockScmNs0wMount,
					mockScmNs1wMount,
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			var engineCfgs []*engine.Config
			for _, sc := range tc.storageCfgs {
				engineCfgs = append(engineCfgs, engine.MockConfig().WithStorage(sc...))
			}
			sCfg := config.DefaultServer().WithEngines(engineCfgs...)
			cs := mockControlService(t, log, sCfg, nil, nil, tc.smsc)

			cs.harness.started.SetTrue()

			outResp, err := cs.getScmUsage(tc.inResp)
			common.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			if diff := cmp.Diff(tc.expOutResp, outResp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
