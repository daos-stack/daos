//
// (C) Copyright 2021-2023 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"os"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
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
		"auto faulty criteria applied": {
			cfg: &Config{
				Tiers: TierConfigs{
					mockScmTier,
					NewTierConfig().WithStorageClass(ClassNvme.String()),
				},
				AutoFaultyProps: BdevAutoFaulty{
					Enable:      true,
					MaxIoErrs:   10000,
					MaxCsumErrs: 20000,
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
				AutoFaultyProps: BdevAutoFaulty{
					Enable:      true,
					MaxIoErrs:   10000,
					MaxCsumErrs: 20000,
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			gotReq, gotErr := BdevWriteConfigRequestFromConfig(test.Context(t), log, tc.cfg,
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

func TestStorage_ProviderUpgradeBdevConfig(t *testing.T) {
	for name, tc := range map[string]struct {
		cfg      *Config
		ctrlrs   NvmeControllers
		bdevProv *mockBdevProvider
		expCalls map[string]int
		expErr   error
	}{
		"one bdev: read fails, write fails": {
			cfg: &Config{
				Tiers: TierConfigs{
					NewTierConfig().WithStorageClass(ClassNvme.String()).WithBdevDeviceList("/dev/loop0"),
				},
			},
			ctrlrs: MockNvmeControllers(1),
			bdevProv: &mockBdevProvider{
				ReadConfigErr:  FaultInvalidSPDKConfig(errors.New("whoops")),
				WriteConfigErr: errors.New("write config failed"),
			},
			expErr: errors.New("write config failed"),
		},
		"one bdev: read fails for something other than invalid spdk config": {
			cfg: &Config{
				Tiers: TierConfigs{
					NewTierConfig().WithStorageClass(ClassNvme.String()).WithBdevDeviceList("/dev/loop0"),
				},
			},
			ctrlrs: MockNvmeControllers(1),
			bdevProv: &mockBdevProvider{
				ReadConfigErr:  errors.New("read config failed"),
				WriteConfigErr: errors.New("write config failed"),
			},
			expErr: errors.New("read config failed"),
		},
		"one bdev: read fails, successful write": {
			cfg: &Config{
				Tiers: TierConfigs{
					NewTierConfig().WithStorageClass(ClassNvme.String()).WithBdevDeviceList("/dev/loop0"),
				},
			},
			ctrlrs: MockNvmeControllers(1),
			bdevProv: &mockBdevProvider{
				ReadConfigErr: FaultInvalidSPDKConfig(errors.New("whoops")),
			},
			expCalls: map[string]int{
				"ReadConfig":  1,
				"WriteConfig": 1,
			},
		},
		"one bdev: no update needed": {
			cfg: &Config{
				Tiers: TierConfigs{
					NewTierConfig().WithStorageClass(ClassNvme.String()).WithBdevDeviceList("/dev/loop0"),
				},
			},
			ctrlrs:   MockNvmeControllers(1),
			bdevProv: &mockBdevProvider{},
			expCalls: map[string]int{
				"ReadConfig": 1,
			},
		},
		"no bdevs: success": {
			cfg: &Config{
				Tiers: TierConfigs{},
			},
			ctrlrs:   NvmeControllers{},
			bdevProv: &mockBdevProvider{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			ctx := test.MustLogContext(t, test.Context(t))

			p := NewProvider(logging.FromContext(ctx), 0, tc.cfg, nil, nil, tc.bdevProv, nil)
			gotErr := p.UpgradeBdevConfig(ctx, tc.ctrlrs)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expCalls, tc.bdevProv.callCounts); diff != "" {
				t.Fatalf("\nunexpected calls (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestStorage_FormatControlMetadata(t *testing.T) {
	for name, tc := range map[string]struct {
		nilProv      bool
		cfg          *Config
		metadataProv MetadataProvider
		expErr       error
	}{
		"nil": {
			nilProv: true,
			expErr:  errors.New("nil"),
		},
		"no control metadata cfg": {
			cfg:          &Config{},
			metadataProv: &MockMetadataProvider{FormatErr: errors.New("format was called!")},
		},
		"format failed": {
			cfg: &Config{
				ControlMetadata: ControlMetadata{
					Path: "something",
				},
			},
			metadataProv: &MockMetadataProvider{FormatErr: errors.New("mock format")},
			expErr:       errors.New("mock format"),
		},
		"success": {
			cfg: &Config{
				ControlMetadata: ControlMetadata{
					Path: "something",
				},
			},
			metadataProv: &MockMetadataProvider{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			var p *Provider
			if !tc.nilProv {
				p = NewProvider(log, 0, tc.cfg, nil, nil, nil, tc.metadataProv)
			}

			err := p.FormatControlMetadata([]uint{0, 1})

			test.CmpErr(t, tc.expErr, err)
		})
	}
}

func TestStorage_ControlMetadataNeedsFormat(t *testing.T) {
	for name, tc := range map[string]struct {
		nilProv      bool
		cfg          *Config
		metadataProv MetadataProvider
		expResult    bool
		expErr       error
	}{
		"nil": {
			nilProv: true,
			expErr:  errors.New("nil"),
		},
		"no control metadata cfg": {
			cfg:          &Config{},
			metadataProv: &MockMetadataProvider{NeedsFormatErr: errors.New("NeedsFormat was called")},
		},
		"NeedsFormat failed": {
			cfg: &Config{
				ControlMetadata: ControlMetadata{
					Path: "something",
				},
			},
			metadataProv: &MockMetadataProvider{NeedsFormatErr: errors.New("mock NeedsFormat")},
			expErr:       errors.New("mock NeedsFormat"),
		},
		"format not needed": {
			cfg: &Config{
				ControlMetadata: ControlMetadata{
					Path: "something",
				},
			},
			metadataProv: &MockMetadataProvider{},
		},
		"format needed": {
			cfg: &Config{
				ControlMetadata: ControlMetadata{
					Path: "something",
				},
			},
			metadataProv: &MockMetadataProvider{
				NeedsFormatRes: true,
			},
			expResult: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			var p *Provider
			if !tc.nilProv {
				p = NewProvider(log, 0, tc.cfg, nil, nil, nil, tc.metadataProv)
			}

			result, err := p.ControlMetadataNeedsFormat()

			test.CmpErr(t, tc.expErr, err)
			test.AssertEqual(t, tc.expResult, result, "")
		})
	}
}

func TestStorage_MountControlMetadata(t *testing.T) {
	for name, tc := range map[string]struct {
		nilProv      bool
		cfg          *Config
		metadataProv MetadataProvider
		scmProv      ScmProvider
		expErr       error
	}{
		"nil": {
			nilProv: true,
			expErr:  errors.New("nil"),
		},
		"no control metadata cfg": {
			cfg: &Config{
				Tiers: TierConfigs{
					{
						Class: "dcpm",
						Scm: ScmConfig{
							MountPoint: "/scm",
							DeviceList: []string{"/dev/pmem0"},
						},
					},
				},
			},
			metadataProv: &MockMetadataProvider{MountErr: errors.New("metadata mount was called!")},
			scmProv:      &MockScmProvider{MountErr: errors.New("scm mount was called!")},
			expErr:       errors.New("scm mount was called!"), // verify that we called into SCM provider
		},
		"mount failed": {
			cfg: &Config{
				ControlMetadata: ControlMetadata{
					Path:       "something",
					DevicePath: "somethingelse",
				},
			},
			metadataProv: &MockMetadataProvider{MountErr: errors.New("mock mount")},
			expErr:       errors.New("mock mount"),
		},
		"success": {
			cfg: &Config{
				ControlMetadata: ControlMetadata{
					Path:       "something",
					DevicePath: "somethingelse",
				},
			},
			metadataProv: &MockMetadataProvider{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			var p *Provider
			if !tc.nilProv {
				p = NewProvider(log, 0, tc.cfg, nil, tc.scmProv, nil, tc.metadataProv)
			}

			err := p.MountControlMetadata()

			test.CmpErr(t, tc.expErr, err)
		})
	}
}

func TestStorage_ControlMetadataIsMounted(t *testing.T) {
	for name, tc := range map[string]struct {
		nilProv    bool
		cfg        *Config
		sysProvCfg *system.MockSysConfig
		expResult  bool
		expErr     error
		expInput   []string
	}{
		"nil provider": {
			nilProv: true,
			expErr:  errors.New("nil"),
		},
		"no control metadata cfg": {
			cfg: &Config{
				Tiers: TierConfigs{
					{
						Class: "dcpm",
						Scm: ScmConfig{
							MountPoint: "/scm",
							DeviceList: []string{"/dev/pmem0"},
						},
					},
				},
			},
			expInput: []string{"/scm"},
		},
		"IsMounted failed": {
			cfg: &Config{
				ControlMetadata: ControlMetadata{
					Path:       "/something",
					DevicePath: "/dev/somethingelse",
				},
			},
			sysProvCfg: &system.MockSysConfig{
				IsMountedErr: errors.New("mock IsMounted"),
			},
			expErr:   errors.New("mock IsMounted"),
			expInput: []string{"/something"},
		},
		"mounted": {
			cfg: &Config{
				ControlMetadata: ControlMetadata{
					Path:       "/something",
					DevicePath: "/dev/somethingelse",
				},
			},
			sysProvCfg: &system.MockSysConfig{
				IsMountedBool: true,
			},
			expResult: true,
			expInput:  []string{"/something"},
		},
		"not mounted": {
			cfg: &Config{
				ControlMetadata: ControlMetadata{
					Path:       "/something",
					DevicePath: "/dev/somethingelse",
				},
			},
			sysProvCfg: &system.MockSysConfig{},
			expResult:  false,
			expInput:   []string{"/something"},
		},
		"no device": {
			cfg: &Config{
				ControlMetadata: ControlMetadata{
					Path: "/something",
				},
			},
			sysProvCfg: &system.MockSysConfig{ // No need to check IsMounted in this case
				IsMountedErr: errors.New("IsMounted was called!"),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			sysProv := system.NewMockSysProvider(log, tc.sysProvCfg)
			var p *Provider
			if !tc.nilProv {
				p = NewProvider(log, 0, tc.cfg, sysProv, nil, nil, nil)
			}

			result, err := p.ControlMetadataIsMounted()

			test.CmpErr(t, tc.expErr, err)
			test.AssertEqual(t, tc.expResult, result, "")

			test.AssertEqual(t, tc.expInput, sysProv.IsMountedInputs, "")
		})
	}
}

func TestStorage_ControlMetadataPath(t *testing.T) {
	for name, tc := range map[string]struct {
		nilProv   bool
		cfg       *Config
		expResult string
	}{
		"nil": {
			nilProv:   true,
			expResult: defaultMetadataPath,
		},
		"control metadata path": {
			cfg: &Config{
				ControlMetadata: ControlMetadata{
					Path:       "/metadata",
					DevicePath: "/dev/dev0",
				},
			},
			expResult: "/metadata/daos_control",
		},
		"no control metadata uses scm": {
			cfg: &Config{
				Tiers: TierConfigs{
					{
						Class: "dcpm",
						Scm: ScmConfig{
							MountPoint: "/scm",
							DeviceList: []string{"/dev/pmem0"},
						},
					},
				},
			},
			expResult: "/scm",
		},
		"no scm mountpoint": {
			cfg: &Config{
				Tiers: TierConfigs{
					{
						Class: "dcpm",
						Scm: ScmConfig{
							DeviceList: []string{"/dev/pmem0"},
						},
					},
				},
			},
			expResult: defaultMetadataPath,
		},
		"no scm tier": {
			cfg: &Config{
				Tiers: TierConfigs{},
			},
			expResult: defaultMetadataPath,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			var p *Provider
			if !tc.nilProv {
				p = NewProvider(log, 0, tc.cfg, nil, nil, nil, nil)
			}

			test.AssertEqual(t, tc.expResult, p.ControlMetadataPath(), "")
		})
	}
}

func TestStorage_ControlMetadataEnginePath(t *testing.T) {
	for name, tc := range map[string]struct {
		nilProv   bool
		engineIdx uint
		cfg       *Config
		expResult string
	}{
		"nil": {
			nilProv:   true,
			expResult: defaultMetadataPath,
		},
		"control metadata path": {
			engineIdx: 2,
			cfg: &Config{
				ControlMetadata: ControlMetadata{
					Path:       "/metadata",
					DevicePath: "/dev/dev0",
				},
			},
			expResult: "/metadata/daos_control/engine2",
		},
		"no control metadata uses scm": {
			cfg: &Config{
				Tiers: TierConfigs{
					{
						Class: "dcpm",
						Scm: ScmConfig{
							MountPoint: "/scm",
							DeviceList: []string{"/dev/pmem0"},
						},
					},
				},
			},
			expResult: "/scm",
		},
		"no scm mountpoint": {
			cfg: &Config{
				Tiers: TierConfigs{
					{
						Class: "dcpm",
						Scm: ScmConfig{
							DeviceList: []string{"/dev/pmem0"},
						},
					},
				},
			},
			expResult: defaultMetadataPath,
		},
		"no scm tier": {
			cfg: &Config{
				Tiers: TierConfigs{},
			},
			expResult: defaultMetadataPath,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			var p *Provider
			if !tc.nilProv {
				p = NewProvider(log, int(tc.engineIdx), tc.cfg, nil, nil, nil, nil)
			}

			if tc.cfg != nil {
				tc.cfg.EngineIdx = tc.engineIdx
			}

			test.AssertEqual(t, tc.expResult, p.ControlMetadataEnginePath(), "")
		})
	}
}
