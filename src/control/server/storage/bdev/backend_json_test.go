//
// (C) Copyright 2021-2024 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"bytes"
	"encoding/json"
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
	disabledRoleBits := 0
	namePostfix := func(i, r int) string {
		return fmt.Sprintf("%s_%d_%d_%d", host, i, tierID, r)
	}
	nvmeName := func(i, roleBits int) string {
		return fmt.Sprintf("Nvme_%s", namePostfix(i, roleBits))
	}
	aioName := func(i, roleBits int) string {
		return fmt.Sprintf("AIO_%s", namePostfix(i, roleBits))
	}
	bdevCfg := func(idx, roleBits int) *SpdkSubsystemConfig {
		return &SpdkSubsystemConfig{
			Method: storage.ConfBdevNvmeAttachController,
			Params: &NvmeAttachControllerParams{
				TransportType:    "PCIe",
				DeviceName:       nvmeName(idx, roleBits),
				TransportAddress: test.MockPCIAddr(int32(idx + 1)),
			},
		}
	}
	multiCtrlrConfs := func(roleBits int, hotplug bool) []*SpdkSubsystemConfig {
		hpParams := &NvmeSetHotplugParams{}
		if hotplug {
			hpParams = &NvmeSetHotplugParams{
				Enable:     true,
				PeriodUsec: uint64((5 * time.Second).Microseconds()),
			}
		}
		return append(defaultSpdkConfig().Subsystems[0].Configs,
			[]*SpdkSubsystemConfig{
				bdevCfg(0, roleBits),
				bdevCfg(1, roleBits),
				{
					Method: storage.ConfBdevNvmeSetHotplug,
					Params: hpParams,
				},
			}...)
	}

	tests := map[string]struct {
		class              storage.Class
		fileSizeGB         int
		devList            []string
		devRoles           int
		enableVmd          bool
		vosEnv             string
		enableHotplug      bool
		busidRange         string
		accelEngine        string
		accelOptMask       storage.AccelOptionBits
		rpcSrvEnable       bool
		rpcSrvSockAddr     string
		autoFaultyEnable   bool
		autoFaultyIO       uint32
		autoFaultyCsum     uint32
		expExtraSubsystems []*SpdkSubsystem
		expBdevCfgs        []*SpdkSubsystemConfig
		expDaosCfgs        []*DaosConfig
		expValidateErr     error
		expErr             error
	}{
		"config validation failure": {
			class:          storage.ClassNvme,
			devList:        []string{"not a pci address"},
			expValidateErr: errors.New("valid PCI addresses"),
		},
		"multiple controllers; roles enabled": {
			class:       storage.ClassNvme,
			devList:     []string{test.MockPCIAddr(1), test.MockPCIAddr(2)},
			devRoles:    storage.BdevRoleAll,
			expBdevCfgs: multiCtrlrConfs(storage.BdevRoleAll, false),
		},
		"multiple controllers; vmd enabled": {
			class:       storage.ClassNvme,
			enableVmd:   true,
			devList:     []string{test.MockPCIAddr(1), test.MockPCIAddr(2)},
			expBdevCfgs: multiCtrlrConfs(0, false),
			expExtraSubsystems: []*SpdkSubsystem{
				{
					Name: "vmd",
					Configs: []*SpdkSubsystemConfig{
						{
							Method: storage.ConfVmdEnable,
							Params: &VmdEnableParams{},
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
			expBdevCfgs:   multiCtrlrConfs(0, true),
			expDaosCfgs: []*DaosConfig{
				{
					Method: storage.ConfSetHotplugBusidRange,
					Params: &HotplugBusidRangeParams{
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
		"AIO file class; multiple files; non-zero file size; roles enabled": {
			class:      storage.ClassFile,
			fileSizeGB: 1,
			devList:    []string{"/path/to/myfile", "/path/to/myotherfile"},
			devRoles:   storage.BdevRoleAll,
			expBdevCfgs: append(defaultSpdkConfig().Subsystems[0].Configs,
				[]*SpdkSubsystemConfig{
					{
						Method: storage.ConfBdevAioCreate,
						Params: &AioCreateParams{
							BlockSize:  humanize.KiByte * 4,
							DeviceName: aioName(0, storage.BdevRoleAll),
							Filename:   "/path/to/myfile",
						},
					},
					{
						Method: storage.ConfBdevAioCreate,
						Params: &AioCreateParams{
							BlockSize:  humanize.KiByte * 4,
							DeviceName: aioName(1, storage.BdevRoleAll),
							Filename:   "/path/to/myotherfile",
						},
					},
					{
						Method: storage.ConfBdevNvmeSetHotplug,
						Params: &NvmeSetHotplugParams{},
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
						Method: storage.ConfBdevAioCreate,
						Params: &AioCreateParams{
							DeviceName: aioName(0, disabledRoleBits),
							Filename:   "/dev/sdb",
						},
					},
					{
						Method: storage.ConfBdevAioCreate,
						Params: &AioCreateParams{
							DeviceName: aioName(1, disabledRoleBits),
							Filename:   "/dev/sdc",
						},
					},
					{
						Method: storage.ConfBdevNvmeSetHotplug,
						Params: &NvmeSetHotplugParams{},
					},
				}...),
			vosEnv: "AIO",
		},
		"multiple controllers; accel, rpc server & auto faulty settings": {
			class:            storage.ClassNvme,
			devList:          []string{test.MockPCIAddr(1), test.MockPCIAddr(2)},
			accelEngine:      storage.AccelEngineSPDK,
			accelOptMask:     storage.AccelOptCRCFlag | storage.AccelOptMoveFlag,
			rpcSrvEnable:     true,
			rpcSrvSockAddr:   "/tmp/spdk.sock",
			autoFaultyEnable: true,
			autoFaultyIO:     100,
			autoFaultyCsum:   200,
			expBdevCfgs:      multiCtrlrConfs(0, false),
			expDaosCfgs: []*DaosConfig{
				{
					Method: storage.ConfSetAccelProps,
					Params: &AccelPropsParams{
						Engine:  storage.AccelEngineSPDK,
						Options: storage.AccelOptCRCFlag | storage.AccelOptMoveFlag,
					},
				},
				{
					Method: storage.ConfSetSpdkRpcServer,
					Params: &SpdkRpcServerParams{
						Enable:   true,
						SockAddr: "/tmp/spdk.sock",
					},
				},
				{
					Method: storage.ConfSetAutoFaultyProps,
					Params: &AutoFaultyParams{
						Enable:      true,
						MaxIoErrs:   100,
						MaxCsumErrs: 200,
					},
				},
			},
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
					DeviceList:  storage.MustNewBdevDeviceList(tc.devList...),
					FileSize:    tc.fileSizeGB,
					BusidRange:  storage.MustNewBdevBusRange(tc.busidRange),
					DeviceRoles: storage.BdevRolesFromBits(tc.devRoles),
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
						WithStorageClass("ram").
						WithScmRamdiskSize(16).
						WithScmMountPoint(mockMntpt),
					cfg,
				).
				WithStorageEnableHotplug(tc.enableHotplug).
				WithTargetCount(8).
				WithPinnedNumaNode(0).
				WithStorageAccelProps(tc.accelEngine, tc.accelOptMask).
				WithStorageSpdkRpcSrvProps(tc.rpcSrvEnable, tc.rpcSrvSockAddr).
				WithStorageAutoFaultyCriteria(tc.autoFaultyEnable, tc.autoFaultyIO,
					tc.autoFaultyCsum)

			if tc.devRoles != 0 {
				engineConfig.Storage.ControlMetadata = storage.ControlMetadata{
					Path: "/opt/daos_md",
				}
			}

			gotValidateErr := engineConfig.Validate() // populate output path
			test.CmpErr(t, tc.expValidateErr, gotValidateErr)
			if tc.expValidateErr != nil {
				return
			}

			writeReq, _ := storage.BdevWriteConfigRequestFromConfig(test.Context(t), log,
				&engineConfig.Storage, tc.enableVmd, storage.MockGetTopology)

			gotCfg, gotErr := newSpdkConfig(log, writeReq)
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

func TestBackend_unreadableSpdkConfig(t *testing.T) {
	testCfg := defaultSpdkConfig()

	buf, err := json.Marshal(testCfg)
	if err != nil {
		t.Fatal(err)
	}
	buf = bytes.Replace(buf, []byte("config"), []byte("bedazzle"), 1)

	if _, err := readSpdkConfig(bytes.NewReader(buf)); err == nil {
		t.Fatal("expected error")
	}
}
