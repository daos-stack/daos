//
// (C) Copyright 2021-2024 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// TestBackend_createAioFile verifies AIO files are created (or not) as expected.
func TestBackend_createAioFile(t *testing.T) {
	tests := map[string]struct {
		path          string
		pathImmutable bool // avoid adjusting path in test if set
		size          uint64
		expErr        error
	}{
		"relative path": {
			path:          "somewhere/bad",
			pathImmutable: true,
			expErr:        errors.New("got relative"),
		},
		"zero size": {
			size:   0,
			expErr: errors.New("zero"),
		},
		"non-existent path": {
			path:   "/timbuk/tu",
			size:   humanize.MiByte,
			expErr: errors.New("no such file or directory"),
		},
		"successful create": {
			path: "/outfile",
			size: humanize.MiByte,
		},
	}

	for name, tc := range tests {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			testDir, clean := test.CreateTestDir(t)
			defer clean()

			if !tc.pathImmutable {
				tc.path = filepath.Join(testDir, tc.path)
			}

			req := &storage.BdevFormatRequest{
				OwnerUID: os.Getuid(),
				OwnerGID: os.Getgid(),
				Properties: storage.BdevTierProperties{
					DeviceFileSize: tc.size,
				},
			}

			gotResp := createAioFile(log, tc.path, req)
			if tc.expErr != nil {
				if gotResp.Error == nil {
					t.Fatal("expected non-nil error in response")
				}
				test.CmpErr(t, tc.expErr, gotResp.Error)
				if _, err := os.Stat(tc.path); err == nil {
					t.Fatalf("%s file was not removed on error", tc.path)
				}

				return
			}
			if gotResp.Error != nil {
				t.Fatal("expected nil error in response")
			}

			expSize := (tc.size / aioBlockSize) * aioBlockSize

			st, err := os.Stat(tc.path)
			if err != nil {
				t.Fatal(err)
			}
			gotSize := st.Size()
			if gotSize != int64(expSize) {
				t.Fatalf("expected %s size to be %d, but got %d",
					tc.path, expSize, gotSize)
			}
		})
	}
}

func TestBackend_writeJSONFile(t *testing.T) {
	tierID := 84
	hostName := "testHost"

	genSpdkCfg := func(in *SpdkConfig, mut func(in *SpdkConfig) *SpdkConfig) *SpdkConfig {
		if mut != nil {
			return mut(in)
		}
		return in
	}
	defaultTestCfg := func() *SpdkConfig {
		return genSpdkCfg(defaultSpdkConfig(), func(in *SpdkConfig) *SpdkConfig {
			in.Subsystems[0].Configs = append(in.Subsystems[0].Configs,
				&SpdkSubsystemConfig{
					Method: "bdev_nvme_attach_controller",
					Params: &NvmeAttachControllerParams{
						TransportType:    "PCIe",
						DeviceName:       "Nvme_testHost_0_84_0",
						TransportAddress: "0000:01:00.0",
					},
				},
				&SpdkSubsystemConfig{
					Method: "bdev_nvme_set_hotplug",
					Params: &NvmeSetHotplugParams{},
				},
			)
			return in
		})
	}

	tests := map[string]struct {
		confIn    *engine.Config
		enableVmd bool
		expCfg    *SpdkConfig
		expErr    error
	}{
		"nvme; single ssds; hotplug enabled": {
			confIn: engine.MockConfig().WithStorage(&storage.TierConfig{
				Tier:  tierID,
				Class: storage.ClassNvme,
				Bdev: storage.BdevConfig{
					DeviceList: storage.MustNewBdevDeviceList(test.MockPCIAddrs(1)...),
				},
			}).WithStorageEnableHotplug(true),
			expCfg: genSpdkCfg(defaultSpdkConfig(), func(in *SpdkConfig) *SpdkConfig {
				in.DaosData.Configs = []*DaosConfig{
					{
						Method: "hotplug_busid_range",
						Params: &HotplugBusidRangeParams{
							Begin: 0,
							End:   7,
						},
					},
				}
				in.Subsystems[0].Configs = append(in.Subsystems[0].Configs,
					&SpdkSubsystemConfig{
						Method: "bdev_nvme_attach_controller",
						Params: &NvmeAttachControllerParams{
							TransportType:    "PCIe",
							DeviceName:       "Nvme_testHost_0_84_0",
							TransportAddress: "0000:01:00.0",
						},
					},
					&SpdkSubsystemConfig{
						Method: "bdev_nvme_set_hotplug",
						Params: &NvmeSetHotplugParams{
							Enable:     true,
							PeriodUsec: 5000000,
						},
					},
				)
				return in
			}),
		},
		"nvme; multiple ssds": {
			confIn: engine.MockConfig().WithStorage(&storage.TierConfig{
				Tier:  tierID,
				Class: storage.ClassNvme,
				Bdev: storage.BdevConfig{
					DeviceList:  storage.MustNewBdevDeviceList(test.MockPCIAddrs(1, 2)...),
					DeviceRoles: storage.BdevRolesFromBits(storage.BdevRoleAll),
				},
			}),
			expCfg: genSpdkCfg(defaultSpdkConfig(), func(in *SpdkConfig) *SpdkConfig {
				in.Subsystems[0].Configs = append(in.Subsystems[0].Configs,
					&SpdkSubsystemConfig{
						Method: "bdev_nvme_attach_controller",
						Params: &NvmeAttachControllerParams{
							TransportType:    "PCIe",
							DeviceName:       "Nvme_testHost_0_84_7",
							TransportAddress: "0000:01:00.0",
						},
					},
					&SpdkSubsystemConfig{
						Method: "bdev_nvme_attach_controller",
						Params: &NvmeAttachControllerParams{
							TransportType:    "PCIe",
							DeviceName:       "Nvme_testHost_1_84_7",
							TransportAddress: "0000:02:00.0",
						},
					},
					&SpdkSubsystemConfig{
						Method: "bdev_nvme_set_hotplug",
						Params: &NvmeSetHotplugParams{},
					},
				)
				return in
			}),
		},
		"nvme; multiple ssds; vmd enabled; bus-id range": {
			confIn: engine.MockConfig().WithStorage(&storage.TierConfig{
				Tier:  tierID,
				Class: storage.ClassNvme,
				Bdev: storage.BdevConfig{
					DeviceList: storage.MustNewBdevDeviceList(test.MockPCIAddrs(1, 2)...),
					BusidRange: storage.MustNewBdevBusRange("0x80-0x8f"),
				},
			}),
			enableVmd: true,
			expCfg: genSpdkCfg(defaultSpdkConfig(), func(in *SpdkConfig) *SpdkConfig {
				in.Subsystems[0].Configs = append(in.Subsystems[0].Configs,
					&SpdkSubsystemConfig{
						Method: "bdev_nvme_attach_controller",
						Params: &NvmeAttachControllerParams{
							TransportType:    "PCIe",
							DeviceName:       "Nvme_testHost_0_84_0",
							TransportAddress: "0000:01:00.0",
						},
					},
					&SpdkSubsystemConfig{
						Method: "bdev_nvme_attach_controller",
						Params: &NvmeAttachControllerParams{
							TransportType:    "PCIe",
							DeviceName:       "Nvme_testHost_1_84_0",
							TransportAddress: "0000:02:00.0",
						},
					},
					&SpdkSubsystemConfig{
						Method: "bdev_nvme_set_hotplug",
						Params: &NvmeSetHotplugParams{},
					},
				)
				in.Subsystems = append(in.Subsystems, &SpdkSubsystem{
					Name: "vmd",
					Configs: []*SpdkSubsystemConfig{
						{
							Method: "enable_vmd",
							Params: &VmdEnableParams{},
						},
					},
				})
				return in
			}),
		},
		"nvme; multiple ssds; hotplug enabled; bus-id range": {
			confIn: engine.MockConfig().WithStorage(&storage.TierConfig{
				Tier:  tierID,
				Class: storage.ClassNvme,
				Bdev: storage.BdevConfig{
					DeviceList: storage.MustNewBdevDeviceList(test.MockPCIAddrs(1, 2)...),
					BusidRange: storage.MustNewBdevBusRange("0x80-0x8f"),
				},
			}).WithStorageEnableHotplug(true),
			expCfg: genSpdkCfg(defaultSpdkConfig(), func(in *SpdkConfig) *SpdkConfig {
				in.DaosData.Configs = []*DaosConfig{
					{
						Method: "hotplug_busid_range",
						Params: &HotplugBusidRangeParams{
							Begin: 128,
							End:   143,
						},
					},
				}
				in.Subsystems[0].Configs = append(in.Subsystems[0].Configs,
					&SpdkSubsystemConfig{
						Method: "bdev_nvme_attach_controller",
						Params: &NvmeAttachControllerParams{
							TransportType:    "PCIe",
							DeviceName:       "Nvme_testHost_0_84_0",
							TransportAddress: "0000:01:00.0",
						},
					},
					&SpdkSubsystemConfig{
						Method: "bdev_nvme_attach_controller",
						Params: &NvmeAttachControllerParams{
							TransportType:    "PCIe",
							DeviceName:       "Nvme_testHost_1_84_0",
							TransportAddress: "0000:02:00.0",
						},
					},
					&SpdkSubsystemConfig{
						Method: "bdev_nvme_set_hotplug",
						Params: &NvmeSetHotplugParams{
							Enable:     true,
							PeriodUsec: 5000000,
						},
					},
				)
				return in
			}),
		},
		"nvme; multiple ssds; vmd and hotplug enabled": {
			confIn: engine.MockConfig().WithStorage(&storage.TierConfig{
				Tier:  tierID,
				Class: storage.ClassNvme,
				Bdev: storage.BdevConfig{
					DeviceList: storage.MustNewBdevDeviceList(test.MockPCIAddrs(1, 2)...),
				},
			}).WithStorageEnableHotplug(true),
			enableVmd: true,
			expCfg: genSpdkCfg(defaultSpdkConfig(), func(in *SpdkConfig) *SpdkConfig {
				in.DaosData.Configs = []*DaosConfig{
					{
						Method: "hotplug_busid_range",
						Params: &HotplugBusidRangeParams{
							Begin: 0,
							End:   255,
						},
					},
				}
				in.Subsystems[0].Configs = append(in.Subsystems[0].Configs,
					&SpdkSubsystemConfig{
						Method: "bdev_nvme_attach_controller",
						Params: &NvmeAttachControllerParams{
							TransportType:    "PCIe",
							DeviceName:       "Nvme_testHost_0_84_0",
							TransportAddress: "0000:01:00.0",
						},
					},
					&SpdkSubsystemConfig{
						Method: "bdev_nvme_attach_controller",
						Params: &NvmeAttachControllerParams{
							TransportType:    "PCIe",
							DeviceName:       "Nvme_testHost_1_84_0",
							TransportAddress: "0000:02:00.0",
						},
					},
					&SpdkSubsystemConfig{
						Method: "bdev_nvme_set_hotplug",
						Params: &NvmeSetHotplugParams{
							Enable:     true,
							PeriodUsec: 5000000,
						},
					},
				)
				in.Subsystems = append(in.Subsystems, &SpdkSubsystem{
					Name: "vmd",
					Configs: []*SpdkSubsystemConfig{
						{
							Method: "enable_vmd",
							Params: &VmdEnableParams{},
						},
					},
				})
				return in
			}),
		},
		"nvme; single controller; acceleration set to none; move and crc opts specified": {
			confIn: engine.MockConfig().WithStorage(&storage.TierConfig{
				Tier:  tierID,
				Class: storage.ClassNvme,
				Bdev: storage.BdevConfig{
					DeviceList: storage.MustNewBdevDeviceList(test.MockPCIAddrs(1)...),
				},
				// Verify default "none" acceleration setting is ignored.
			}).WithStorageAccelProps(storage.AccelEngineNone,
				storage.AccelOptCRCFlag|storage.AccelOptMoveFlag),
			expCfg: defaultTestCfg(),
		},
		"nvme; single controller; acceleration set to spdk; no opts specified": {
			confIn: engine.MockConfig().WithStorage(&storage.TierConfig{
				Tier:  tierID,
				Class: storage.ClassNvme,
				Bdev: storage.BdevConfig{
					DeviceList: storage.MustNewBdevDeviceList(test.MockPCIAddrs(1)...),
				},
				// Verify default "spdk" acceleration setting with no enable options is ignored.
			}).WithStorageAccelProps(storage.AccelEngineSPDK, 0),
			expCfg: defaultTestCfg(),
		},
		"nvme; single controller; auto faulty disabled but criteria set": {
			confIn: engine.MockConfig().WithStorage(&storage.TierConfig{
				Tier:  tierID,
				Class: storage.ClassNvme,
				Bdev: storage.BdevConfig{
					DeviceList: storage.MustNewBdevDeviceList(test.MockPCIAddrs(1)...),
				},
				// Verify "false" auto faulty setting is ignored.
			}).WithStorageAutoFaultyCriteria(false, 100, 200),
			expCfg: defaultTestCfg(),
		},
		"nvme; single controller; accel set with opts; rpc srv set; auto faulty criteria": {
			confIn: engine.MockConfig().WithStorage(&storage.TierConfig{
				Tier:  tierID,
				Class: storage.ClassNvme,
				Bdev: storage.BdevConfig{
					DeviceList: storage.MustNewBdevDeviceList(test.MockPCIAddrs(1)...),
				},
			}).
				WithStorageAccelProps(storage.AccelEngineSPDK,
					storage.AccelOptCRCFlag|storage.AccelOptMoveFlag).
				WithStorageSpdkRpcSrvProps(true, "/tmp/spdk.sock").
				WithStorageAutoFaultyCriteria(true, 100, 200),
			expCfg: genSpdkCfg(defaultTestCfg(), func(in *SpdkConfig) *SpdkConfig {
				in.DaosData.Configs = []*DaosConfig{
					{
						Method: "accel_props",
						Params: &AccelPropsParams{
							Engine:  "spdk",
							Options: 3,
						},
					},
					{
						Method: "spdk_rpc_srv",
						Params: &SpdkRpcServerParams{
							Enable:   true,
							SockAddr: "/tmp/spdk.sock",
						},
					},
					{
						Method: "auto_faulty",
						Params: &AutoFaultyParams{
							Enable:      true,
							MaxIoErrs:   100,
							MaxCsumErrs: 200,
						},
					},
				}
				return in
			}),
		},
	}
	for name, tc := range tests {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			testDir, clean := test.CreateTestDir(t)
			defer clean()

			cfgOutputPath := filepath.Join(testDir, "outfile")

			req, err := storage.BdevWriteConfigRequestFromConfig(test.Context(t), log,
				&(tc.confIn.WithStorageConfigOutputPath(cfgOutputPath)).Storage,
				tc.enableVmd, storage.MockGetTopology)
			if err != nil {
				t.Fatal(err)
			}
			req.Hostname = hostName

			gotErr := writeJsonConfig(log, req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			r, err := os.Open(cfgOutputPath)
			if err != nil {
				t.Fatal(err)
			}
			gotCfg, err := readSpdkConfig(r)
			if err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(tc.expCfg, gotCfg); diff != "" {
				t.Fatalf("unexpected config after write (-want, +got):\n%s", diff)
			}
		})
	}
}
