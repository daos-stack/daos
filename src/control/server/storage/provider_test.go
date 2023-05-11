//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"context"
	"os"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

var mockScmTier = NewTierConfig().WithStorageClass(ClassDcpm.String()).
	WithScmMountPoint("/mnt/daos0").
	WithScmDeviceList("/dev/pmem0")

// defBdevCmpOpts returns a default set of cmp option suitable for this package
func defBdevCmpOpts() []cmp.Option {
	return []cmp.Option{
		// ignore these fields on most tests, as they are intentionally not stable
		cmpopts.IgnoreFields(NvmeController{}, "HealthStats", "Serial"),
	}
}

func Test_scanBdevsTiers(t *testing.T) {
	for name, tc := range map[string]struct {
		direct     bool
		vmdEnabled bool
		cfg        *Config
		cache      *BdevScanResponse
		scanResp   *BdevScanResponse
		scanErr    error
		expResults []BdevTierScanResult
		expErr     error
	}{
		"nil cfg": {
			expErr: errors.New("nil storage config"),
		},
		"nil cfg tiers": {
			cfg:    new(Config),
			expErr: errors.New("nil storage config tiers"),
		},
		"no bdev configs": {
			cfg: &Config{
				Tiers: TierConfigs{mockScmTier},
			},
			expErr: errors.New("no bdevs in config"),
		},
		"nil scan cache": {
			cfg: &Config{
				Tiers: TierConfigs{
					mockScmTier,
					NewTierConfig().WithStorageClass(ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(3)),
				},
			},
			expResults: []BdevTierScanResult{
				{
					Result: &BdevScanResponse{
						Controllers: NvmeControllers{},
					},
				},
			},
		},
		"bypass cache; missing controller": {
			direct: true,
			cfg: &Config{
				Tiers: TierConfigs{
					mockScmTier,
					NewTierConfig().WithStorageClass(ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(3)),
				},
			},
			cache: &BdevScanResponse{
				Controllers: MockNvmeControllers(3),
			},
			scanResp: &BdevScanResponse{
				Controllers: MockNvmeControllers(2),
			},
			expResults: []BdevTierScanResult{
				{
					Result: &BdevScanResponse{
						Controllers: NvmeControllers{},
					},
				},
			},
		},
		"bypass cache": {
			direct: true,
			cfg: &Config{
				Tiers: TierConfigs{
					mockScmTier,
					NewTierConfig().WithStorageClass(ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(2)),
				},
			},
			cache: &BdevScanResponse{
				Controllers: MockNvmeControllers(2),
			},
			scanResp: &BdevScanResponse{
				Controllers: MockNvmeControllers(3),
			},
			expResults: []BdevTierScanResult{
				{
					Result: &BdevScanResponse{
						Controllers: []*NvmeController{
							MockNvmeController(2),
						},
					},
				},
			},
		},
		"bypass cache; scan error": {
			direct: true,
			cfg: &Config{
				Tiers: TierConfigs{
					mockScmTier,
					NewTierConfig().WithStorageClass(ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(2)),
				},
			},
			scanResp: &BdevScanResponse{
				Controllers: MockNvmeControllers(3),
			},
			scanErr: errors.New("fail"),
			expErr:  errors.New("fail"),
		},
		"use cache; missing controller": {
			cfg: &Config{
				Tiers: TierConfigs{
					mockScmTier,
					NewTierConfig().WithStorageClass(ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(2)),
				},
			},
			cache: &BdevScanResponse{
				Controllers: MockNvmeControllers(2),
			},
			scanResp: &BdevScanResponse{
				Controllers: MockNvmeControllers(3),
			},
			expResults: []BdevTierScanResult{
				{
					Result: &BdevScanResponse{
						Controllers: []*NvmeController{},
					},
				},
			},
		},
		"use cache": {
			cfg: &Config{
				Tiers: TierConfigs{
					mockScmTier,
					NewTierConfig().WithStorageClass(ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(2)),
				},
			},
			cache: &BdevScanResponse{
				Controllers: MockNvmeControllers(3),
			},
			expResults: []BdevTierScanResult{
				{
					Result: &BdevScanResponse{
						Controllers: []*NvmeController{
							MockNvmeController(2),
						},
					},
				},
			},
		},
		"multi-tier; bypass cache": {
			direct: true,
			cfg: &Config{
				Tiers: TierConfigs{
					mockScmTier,
					NewTierConfig().WithStorageClass(ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(2), test.MockPCIAddr(3)),
					NewTierConfig().WithStorageClass(ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(4), test.MockPCIAddr(5)),
				},
			},
			scanResp: &BdevScanResponse{
				Controllers: MockNvmeControllers(6),
			},
			expResults: []BdevTierScanResult{
				{
					Result: &BdevScanResponse{
						Controllers: []*NvmeController{
							MockNvmeController(2), MockNvmeController(3),
						},
					},
				},
				{
					Result: &BdevScanResponse{
						Controllers: []*NvmeController{
							MockNvmeController(4), MockNvmeController(5),
						},
					},
				},
			},
		},
		"multi-tier; use cache": {
			cfg: &Config{
				Tiers: TierConfigs{
					mockScmTier,
					NewTierConfig().WithStorageClass(ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(2), test.MockPCIAddr(3)),
					NewTierConfig().WithStorageClass(ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(4), test.MockPCIAddr(5)),
				},
			},
			cache: &BdevScanResponse{
				Controllers: MockNvmeControllers(6),
			},
			expResults: []BdevTierScanResult{
				{
					Result: &BdevScanResponse{
						Controllers: []*NvmeController{
							MockNvmeController(2), MockNvmeController(3),
						},
					},
				},
				{
					Result: &BdevScanResponse{
						Controllers: []*NvmeController{
							MockNvmeController(4), MockNvmeController(5),
						},
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			scanFn := func(r BdevScanRequest) (*BdevScanResponse, error) {
				return tc.scanResp, tc.scanErr
			}

			gotResults, gotErr := scanBdevTiers(log, tc.vmdEnabled, tc.direct, tc.cfg, tc.cache, scanFn)
			test.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResults, gotResults, defBdevCmpOpts()...); diff != "" {
				t.Fatalf("\nunexpected results (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func Test_BdevWriteRequestFromConfig(t *testing.T) {
	hostname, err := os.Hostname()
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		cfg        *Config
		vmdEnabled bool
		getTopoFn  topologyGetter
		expReq     *BdevWriteConfigRequest
		expErr     error
	}{
		"nil config": {
			expErr: errors.New("nil config"),
		},
		"nil topo function": {
			cfg:    &Config{},
			expErr: errors.New("nil GetTopology"),
		},
		"no bdev configs": {
			cfg: &Config{
				Tiers:         TierConfigs{mockScmTier},
				EnableHotplug: true,
			},
			getTopoFn: MockGetTopology,
			expReq: &BdevWriteConfigRequest{
				OwnerUID:       os.Geteuid(),
				OwnerGID:       os.Getegid(),
				TierProps:      []BdevTierProperties{},
				Hostname:       hostname,
				HotplugEnabled: true,
			},
		},
		"hotplug disabled": {
			cfg: &Config{
				Tiers: TierConfigs{
					mockScmTier,
					NewTierConfig().WithStorageClass(ClassNvme.String()).
						WithBdevBusidRange("0x00-0x7f"),
				},
			},
			getTopoFn: MockGetTopology,
			expReq: &BdevWriteConfigRequest{
				OwnerUID: os.Geteuid(),
				OwnerGID: os.Getegid(),
				TierProps: []BdevTierProperties{
					{Class: ClassNvme},
				},
				Hostname: hostname,
			},
		},
		"range specified": {
			cfg: &Config{
				Tiers: TierConfigs{
					mockScmTier,
					NewTierConfig().WithStorageClass(ClassNvme.String()).
						WithBdevBusidRange("0x70-0x7f"),
				},
				EnableHotplug: true,
			},
			getTopoFn: MockGetTopology,
			expReq: &BdevWriteConfigRequest{
				OwnerUID: os.Geteuid(),
				OwnerGID: os.Getegid(),
				TierProps: []BdevTierProperties{
					{Class: ClassNvme},
				},
				Hostname:          hostname,
				HotplugEnabled:    true,
				HotplugBusidBegin: 0x70,
				HotplugBusidEnd:   0x7f,
			},
		},
		"range specified; vmd enabled": {
			cfg: &Config{
				Tiers: TierConfigs{
					mockScmTier,
					NewTierConfig().WithStorageClass(ClassNvme.String()).
						WithBdevBusidRange("0x70-0x7f"),
				},
				EnableHotplug: true,
			},
			vmdEnabled: true,
			getTopoFn:  MockGetTopology,
			expReq: &BdevWriteConfigRequest{
				OwnerUID: os.Geteuid(),
				OwnerGID: os.Getegid(),
				TierProps: []BdevTierProperties{
					{Class: ClassNvme},
				},
				Hostname:          hostname,
				HotplugEnabled:    true,
				HotplugBusidBegin: 0x00,
				HotplugBusidEnd:   0xff,
				VMDEnabled:        true,
			},
		},
		"range unspecified": {
			cfg: &Config{
				Tiers: TierConfigs{
					mockScmTier,
					NewTierConfig().WithStorageClass(ClassNvme.String()),
				},
				EnableHotplug: true,
			},
			getTopoFn: MockGetTopology,
			expReq: &BdevWriteConfigRequest{
				OwnerUID: os.Geteuid(),
				OwnerGID: os.Getegid(),
				TierProps: []BdevTierProperties{
					{Class: ClassNvme},
				},
				Hostname:        hostname,
				HotplugEnabled:  true,
				HotplugBusidEnd: 0x07,
			},
		},
		"range unspecified; vmd enabled": {
			cfg: &Config{
				Tiers: TierConfigs{
					mockScmTier,
					NewTierConfig().WithStorageClass(ClassNvme.String()),
				},
				EnableHotplug: true,
			},
			vmdEnabled: true,
			getTopoFn:  MockGetTopology,
			expReq: &BdevWriteConfigRequest{
				OwnerUID: os.Geteuid(),
				OwnerGID: os.Getegid(),
				TierProps: []BdevTierProperties{
					{Class: ClassNvme},
				},
				Hostname:          hostname,
				HotplugEnabled:    true,
				HotplugBusidBegin: 0x00,
				HotplugBusidEnd:   0xff,
				VMDEnabled:        true,
			},
		},
		"accel properties; spdk": {
			cfg: &Config{
				Tiers: TierConfigs{
					mockScmTier,
					NewTierConfig().WithStorageClass(ClassNvme.String()),
				},
				AccelProps: AccelProps{
					Engine:  AccelEngineSPDK,
					Options: AccelOptCRCFlag | AccelOptMoveFlag,
				},
			},
			getTopoFn: MockGetTopology,
			expReq: &BdevWriteConfigRequest{
				OwnerUID: os.Geteuid(),
				OwnerGID: os.Getegid(),
				TierProps: []BdevTierProperties{
					{Class: ClassNvme},
				},
				Hostname: hostname,
				AccelProps: AccelProps{
					Engine:  AccelEngineSPDK,
					Options: AccelOptCRCFlag | AccelOptMoveFlag,
				},
			},
		},
		"spdk rpc server enabled": {
			cfg: &Config{
				Tiers: TierConfigs{
					mockScmTier,
					NewTierConfig().WithStorageClass(ClassNvme.String()),
				},
				SpdkRpcSrvProps: SpdkRpcServer{
					Enable:   true,
					SockAddr: "/tmp/spdk.sock",
				},
			},
			getTopoFn: MockGetTopology,
			expReq: &BdevWriteConfigRequest{
				OwnerUID: os.Geteuid(),
				OwnerGID: os.Getegid(),
				TierProps: []BdevTierProperties{
					{Class: ClassNvme},
				},
				Hostname: hostname,
				SpdkRpcSrvProps: SpdkRpcServer{
					Enable:   true,
					SockAddr: "/tmp/spdk.sock",
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			gotReq, gotErr := BdevWriteConfigRequestFromConfig(context.TODO(), log, tc.cfg,
				tc.vmdEnabled, tc.getTopoFn)
			test.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expReq, gotReq, defBdevCmpOpts()...); diff != "" {
				t.Fatalf("\nunexpected generated request (-want, +got):\n%s\n", diff)
			}
		})
	}
}
