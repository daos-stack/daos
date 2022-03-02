//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"context"
	"fmt"
	"os"
	"testing"
	"time"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// TestBackend_newSpdkConfig verifies config parameters for bdev get
// converted into config content that can be consumed by spdk.
func TestBackend_newSpdkConfig(t *testing.T) {
	mockMntpt := "/mock/mnt/daos"
	tierID := 84
	host, _ := os.Hostname()

	multiCtrlrConfs := func() []*SpdkSubsystemConfig {
		return append(defaultSpdkConfig().Subsystems[0].Configs,
			[]*SpdkSubsystemConfig{
				{
					Method: SpdkBdevNvmeAttachController,
					Params: NvmeAttachControllerParams{
						TransportType:    "PCIe",
						DeviceName:       fmt.Sprintf("Nvme_%s_0_%d", host, tierID),
						TransportAddress: test.MockPCIAddr(1),
					},
				},
				{
					Method: SpdkBdevNvmeAttachController,
					Params: NvmeAttachControllerParams{
						TransportType:    "PCIe",
						DeviceName:       fmt.Sprintf("Nvme_%s_1_%d", host, tierID),
						TransportAddress: test.MockPCIAddr(2),
					},
				},
			}...)
	}

	hotplugConfs := multiCtrlrConfs()
	hotplugConfs[2].Params = NvmeSetHotplugParams{
		Enable: true, PeriodUsec: uint64((5 * time.Second).Microseconds()),
	}

	tests := map[string]struct {
		class              storage.Class
		fileSizeGB         int
		devList            []string
		enableVmd          bool
		enableHotplug      bool
		busidRange         string
		vosEnv             string
		expExtraSubsystems []*SpdkSubsystem
		expBdevCfgs        []*SpdkSubsystemConfig
		expDaosCfgs        []*SpdkDaosConfig
		expValidateErr     error
		expErr             error
	}{
		"config validation failure": {
			class:          storage.ClassNvme,
			devList:        []string{"not a pci address"},
			expValidateErr: errors.New("valid PCI addresses"),
		},
		"multiple controllers": {
			class:       storage.ClassNvme,
			devList:     []string{test.MockPCIAddr(1), test.MockPCIAddr(2)},
			expBdevCfgs: multiCtrlrConfs(),
		},
		"multiple controllers; vmd enabled": {
			class:       storage.ClassNvme,
			enableVmd:   true,
			devList:     []string{test.MockPCIAddr(1), test.MockPCIAddr(2)},
			expBdevCfgs: multiCtrlrConfs(),
			expExtraSubsystems: []*SpdkSubsystem{
				{
					Name: "vmd",
					Configs: []*SpdkSubsystemConfig{
						{
							Method: SpdkVmdEnable,
							Params: VmdEnableParams{},
						},
					},
				},
			},
		},
		"multiple controllers; hotplug enabled; bus-id range specified": {
			class:         storage.ClassNvme,
			devList:       []string{test.MockPCIAddr(1), test.MockPCIAddr(2)},
			enableHotplug: true,
			busidRange:    "0x8a-0x8f",
			expBdevCfgs:   hotplugConfs,
			expDaosCfgs: []*SpdkDaosConfig{
				{
					Method: SpdkHotplugBusidRange,
					Params: HotplugBusidRangeParams{
						Begin: 138, End: 143,
					},
				},
			},
		},
		"AIO file class; multiple files; zero file size": {
			class:          storage.ClassFile,
			devList:        []string{"/path/to/myfile", "/path/to/myotherfile"},
			expValidateErr: errors.New("requires non-zero bdev_size"),
		},
		"AIO file class; multiple files; non-zero file size": {
			class:      storage.ClassFile,
			fileSizeGB: 1,
			devList:    []string{"/path/to/myfile", "/path/to/myotherfile"},
			expBdevCfgs: append(defaultSpdkConfig().Subsystems[0].Configs,
				[]*SpdkSubsystemConfig{
					{
						Method: SpdkBdevAioCreate,
						Params: AioCreateParams{
							BlockSize:  humanize.KiByte * 4,
							DeviceName: fmt.Sprintf("AIO_%s_0_%d", host, tierID),
							Filename:   "/path/to/myfile",
						},
					},
					{
						Method: SpdkBdevAioCreate,
						Params: AioCreateParams{
							BlockSize:  humanize.KiByte * 4,
							DeviceName: fmt.Sprintf("AIO_%s_1_%d", host, tierID),
							Filename:   "/path/to/myotherfile",
						},
					},
				}...),
			vosEnv: "AIO",
		},
		"AIO kdev class; multiple devices": {
			class:   storage.ClassKdev,
			devList: []string{"/dev/sdb", "/dev/sdc"},
			expBdevCfgs: append(defaultSpdkConfig().Subsystems[0].Configs,
				[]*SpdkSubsystemConfig{
					{
						Method: SpdkBdevAioCreate,
						Params: AioCreateParams{
							DeviceName: fmt.Sprintf("AIO_%s_0_%d", host, tierID),
							Filename:   "/dev/sdb",
						},
					},
					{
						Method: SpdkBdevAioCreate,
						Params: AioCreateParams{
							DeviceName: fmt.Sprintf("AIO_%s_1_%d", host, tierID),
							Filename:   "/dev/sdc",
						},
					},
				}...),
			vosEnv: "AIO",
		},
	}

	for name, tc := range tests {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			cfg := &storage.TierConfig{
				Tier:  tierID,
				Class: storage.ClassNvme,
				Bdev: storage.BdevConfig{
					DeviceList: storage.MustNewBdevDeviceList(tc.devList...),
					FileSize:   tc.fileSizeGB,
					BusidRange: storage.MustNewBdevBusRange(tc.busidRange),
				},
			}
			if tc.class != "" {
				cfg.Class = tc.class
			}
			if tc.vosEnv == "" {
				tc.vosEnv = "NVME"
			}

			engineConfig := engine.MockConfig().
				WithFabricProvider("test"). // valid enough to pass "not-blank" test
				WithFabricInterface("ib0"). // ib0 recognized by mock validator
				WithFabricInterfacePort(42).
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("dcpm").
						WithScmDeviceList("foo").
						WithScmMountPoint(mockMntpt),
					cfg,
				).
				WithStorageEnableHotplug(tc.enableHotplug).
				WithPinnedNumaNode(0)

			gotValidateErr := engineConfig.Validate(log, nil) // populate output path
			test.CmpErr(t, tc.expValidateErr, gotValidateErr)
			if tc.expValidateErr != nil {
				return
			}

			writeReq, _ := storage.BdevWriteConfigRequestFromConfig(context.TODO(), log,
				&engineConfig.Storage, tc.enableVmd, storage.MockGetTopology)

			gotCfg, gotErr := newSpdkConfig(log, &writeReq)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			expCfg := defaultSpdkConfig()
			if tc.expExtraSubsystems != nil {
				expCfg.Subsystems = append(expCfg.Subsystems, tc.expExtraSubsystems...)
			}
			if tc.expBdevCfgs != nil {
				expCfg.Subsystems[0].Configs = tc.expBdevCfgs
			}
			if tc.expDaosCfgs != nil {
				expCfg.DaosData.Configs = tc.expDaosCfgs
			}

			if diff := cmp.Diff(expCfg, gotCfg); diff != "" {
				t.Fatalf("(-want, +got):\n%s", diff)
			}

			if engineConfig.Storage.VosEnv != tc.vosEnv {
				t.Fatalf("expected VosEnv to be %q, but it was %q", tc.vosEnv,
					engineConfig.Storage.VosEnv)
			}
		})
	}
}
