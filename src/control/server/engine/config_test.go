//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package engine

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"
	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/server/storage"
)

var update = flag.Bool("update", false, "update .golden files")

var defConfigCmpOpts = []cmp.Option{
	cmpopts.SortSlices(func(a, b string) bool { return a < b }),
	cmp.Comparer(func(x, y *storage.BdevDeviceList) bool {
		if x == nil && y == nil {
			return true
		}
		return x.Equals(y)
	}),
}

func TestConfig_HasEnvVar(t *testing.T) {
	for name, tc := range map[string]struct {
		startVars []string
		addVar    string
		addVal    string
		expVars   []string
	}{
		"empty": {
			addVar:  "FOO",
			addVal:  "BAR",
			expVars: []string{"FOO=BAR"},
		},
		"similar prefix": {
			startVars: []string{"FOO_BAR=BAZ"},
			addVar:    "FOO",
			addVal:    "BAR",
			expVars:   []string{"FOO_BAR=BAZ", "FOO=BAR"},
		},
		"same prefix": {
			startVars: []string{"FOO=BAZ"},
			addVar:    "FOO",
			addVal:    "BAR",
			expVars:   []string{"FOO=BAZ"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			cfg := MockConfig().
				WithEnvVars(tc.startVars...)

			if !cfg.HasEnvVar(tc.addVar) {
				cfg.WithEnvVars(tc.addVar + "=" + tc.addVal)
			}

			if diff := cmp.Diff(tc.expVars, cfg.EnvVars, defConfigCmpOpts...); diff != "" {
				t.Fatalf("unexpected env vars:\n%s\n", diff)
			}
		})
	}
}

func TestConfig_GetEnvVar(t *testing.T) {
	for name, tc := range map[string]struct {
		environment []string
		key         string
		expValue    string
		expErr      error
	}{
		"present": {
			environment: []string{"FOO=BAR"},
			key:         "FOO",
			expValue:    "BAR",
		},
		"invalid prefix": {
			environment: []string{"FOO=BAR"},
			key:         "FFOO",
			expErr:      errors.New("Undefined environment variable"),
		},
		"invalid suffix": {
			environment: []string{"FOO=BAR"},
			key:         "FOOO",
			expErr:      errors.New("Undefined environment variable"),
		},
		"empty env": {
			key:    "FOO",
			expErr: errors.New("Undefined environment variable"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			cfg := MockConfig().WithEnvVars(tc.environment...)

			value, err := cfg.GetEnvVar(tc.key)

			if err != nil {
				test.AssertTrue(t, tc.expErr != nil,
					fmt.Sprintf("Unexpected error %q", err))
				test.CmpErr(t, tc.expErr, err)
				test.AssertEqual(t, value, "",
					fmt.Sprintf("Unexpected value %q for key %q",
						tc.key, value))
				return
			}

			test.AssertTrue(t, tc.expErr == nil,
				fmt.Sprintf("Expected error %q", tc.expErr))
			test.AssertEqual(t, value, tc.expValue, "Invalid value returned")
		})
	}
}

func TestConfig_Constructed(t *testing.T) {
	goldenPath := "testdata/full.golden"

	// just set all values regardless of validity
	constructed := MockConfig().
		WithFabricProvider("foo+bar").
		WithFabricInterface("qib42"). // qib42 recognized by mock validator
		WithFabricInterfacePort(100).
		WithModules("foo,bar,baz").
		WithStorage(
			storage.NewTierConfig().
				WithStorageClass("ram").
				WithScmRamdiskSize(42).
				WithScmMountPoint("/mnt/daostest").
				WithScmDeviceList("/dev/a", "/dev/b"),
			storage.NewTierConfig().
				WithStorageClass("kdev").
				WithBdevDeviceCount(2).
				WithBdevFileSize(20).
				WithBdevDeviceList("/dev/c", "/dev/d"),
		).
		WithLogFile("/path/to/log").
		WithLogMask("DD_DEBUG").
		WithEnvVars("FOO=BAR", "BAZ=QUX").
		WithServiceThreadCore(8).
		WithTargetCount(12).
		WithHelperStreamCount(1).
		WithPinnedNumaNode(8).
		WithBypassHealthChk(nil)

	if *update {
		outFile, err := os.Create(goldenPath)
		if err != nil {
			t.Fatal(err)
		}
		e := yaml.NewEncoder(outFile)
		if err := e.Encode(constructed); err != nil {
			t.Fatal(err)
		}
		outFile.Close()
	}

	fromDisk := &Config{}
	file, err := os.Open(goldenPath)
	if err != nil {
		t.Fatal(err)
	}
	d := yaml.NewDecoder(file)
	if err := d.Decode(fromDisk); err != nil {
		t.Fatal(err)
	}

	if diff := cmp.Diff(fromDisk, constructed, defConfigCmpOpts...); diff != "" {
		t.Fatalf("(-want, +got):\n%s", diff)
	}
}

func TestConfig_ScmValidation(t *testing.T) {
	baseValidConfig := func() *Config {
		return MockConfig().
			WithFabricProvider("test"). // valid enough to pass "not-blank" test
			WithFabricInterface("ib0"). // ib0 recognized by mock validator
			WithFabricInterfacePort(42).
			WithTargetCount(8).
			WithPinnedNumaNode(0)
	}

	for name, tc := range map[string]struct {
		cfg    *Config
		expErr error
	}{
		"missing storage class": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithScmMountPoint("test"),
				),
			expErr: storage.FaultScmConfigTierMissing,
		},
		"missing scm_mount": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("ram"),
				),
			expErr: errors.New("scm_mount"),
		},
		"ramdisk valid": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("ram").
						WithScmRamdiskSize(storage.MinRamdiskMem).
						WithScmMountPoint("test"),
				),
		},
		"ramdisk with scm_list": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("ram").
						WithScmRamdiskSize(1).
						WithScmDeviceList("foo", "bar").
						WithScmMountPoint("test"),
				),
			expErr: errors.New("scm_list"),
		},
		"dcpm valid": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("dcpm").
						WithScmDeviceList("foo").
						WithScmMountPoint("test"),
				),
		},
		"dcpm scm_list too long": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("dcpm").
						WithScmDeviceList("foo", "bar").
						WithScmMountPoint("test"),
				),
			expErr: errors.New("scm_list"),
		},
		"dcpm scm_list empty": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("dcpm").
						WithScmMountPoint("test"),
				),
			expErr: errors.New("scm_list"),
		},
		"dcpm with scm_size": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("dcpm").
						WithScmDeviceList("foo").
						WithScmRamdiskSize(1).
						WithScmMountPoint("test"),
				),
			expErr: errors.New("scm_size"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.CmpErr(t, tc.expErr, tc.cfg.Validate())
		})
	}
}

func TestConfig_BdevValidation(t *testing.T) {
	baseValidConfig := func() *Config {
		return MockConfig().
			WithFabricProvider("test"). // valid enough to pass "not-blank" test
			WithFabricInterface("ib0"). // ib0 recognized by mock validator
			WithFabricInterfacePort(42).
			WithStorage(
				storage.NewTierConfig().
					WithStorageClass("dcpm").
					WithScmDeviceList("foo").
					WithScmMountPoint("test"),
			).
			WithTargetCount(8).
			WithPinnedNumaNode(0)
	}

	for name, tc := range map[string]struct {
		cfg             *Config
		expErr          error
		expCls          storage.Class
		expEmptyCfgPath bool
	}{
		"nvme class; no scm": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("nvme").
						WithBdevDeviceList(test.MockPCIAddr(1), test.MockPCIAddr(2)),
				),
			expErr: errors.New("missing scm storage tier"),
		},
		"unknown class": {
			cfg: baseValidConfig().
				AppendStorage(
					storage.NewTierConfig().
						WithStorageClass("nvmed"),
				),
			expErr: errors.New("no storage class"),
		},
		"nvme class; no devices": {
			cfg: baseValidConfig().
				AppendStorage(
					storage.NewTierConfig().
						WithStorageClass("nvme"),
				),
			expErr: errors.New("valid PCI addresses"),
		},
		"nvme class; good pci addresses": {
			cfg: baseValidConfig().
				AppendStorage(
					storage.NewTierConfig().
						WithStorageClass("nvme").
						WithBdevDeviceList(test.MockPCIAddr(1), test.MockPCIAddr(2)),
				),
		},
		"nvme class; duplicate pci address": {
			cfg: baseValidConfig().
				AppendStorage(
					storage.NewTierConfig().
						WithStorageClass("nvme").
						WithBdevDeviceList(test.MockPCIAddr(1), test.MockPCIAddr(1)),
				),
			expErr: errors.New("bdev_list"),
		},
		"nvme class; bad pci address": {
			cfg: baseValidConfig().
				AppendStorage(
					storage.NewTierConfig().
						WithStorageClass("nvme").
						WithBdevDeviceList(test.MockPCIAddr(1), "0000:00:00"),
				),
			expErr: errors.New("valid PCI addresses"),
		},
		"kdev class; no devices": {
			cfg: baseValidConfig().
				AppendStorage(
					storage.NewTierConfig().
						WithStorageClass("kdev"),
				),
			expErr: errors.New("kdev requires non-empty bdev_list"),
		},
		"kdev class; valid": {
			cfg: baseValidConfig().
				AppendStorage(
					storage.NewTierConfig().
						WithStorageClass("kdev").
						WithBdevDeviceList("/dev/sda"),
				),
			expCls: storage.ClassKdev,
		},
		"file class; no size": {
			cfg: baseValidConfig().
				AppendStorage(
					storage.NewTierConfig().
						WithStorageClass("file").
						WithBdevDeviceList("bdev1"),
				),
			expErr: errors.New("file requires non-zero bdev_size"),
		},
		"file class; negative size": {
			cfg: baseValidConfig().
				AppendStorage(
					storage.NewTierConfig().
						WithStorageClass("file").
						WithBdevDeviceList("bdev1").
						WithBdevFileSize(-1),
				),
			expErr: errors.New("negative bdev_size"),
		},
		"file class; no devices": {
			cfg: baseValidConfig().
				AppendStorage(
					storage.NewTierConfig().
						WithStorageClass("file").
						WithBdevFileSize(10),
				),
			expErr: errors.New("file requires non-empty bdev_list"),
		},
		"file class; valid": {
			cfg: baseValidConfig().
				AppendStorage(
					storage.NewTierConfig().
						WithStorageClass("file").
						WithBdevFileSize(10).
						WithBdevDeviceList("bdev1", "bdev2"),
				),
			expCls: storage.ClassFile,
		},
		"mix of emulated and non-emulated device classes": {
			cfg: baseValidConfig().
				AppendStorage(
					storage.NewTierConfig().
						WithStorageClass("nvme").
						WithBdevDeviceList(test.MockPCIAddr(1)),
					storage.NewTierConfig().
						WithStorageClass("file").
						WithBdevFileSize(10).
						WithBdevDeviceList("bdev1", "bdev2"),
				),
			expErr: storage.FaultBdevConfigTierTypeMismatch,
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.CmpErr(t, tc.expErr, tc.cfg.Validate())
			if tc.expErr != nil {
				return
			}

			var ecp string
			if !tc.expEmptyCfgPath {
				if tc.expCls == "" {
					tc.expCls = storage.ClassNvme // default if unset
				}
				test.AssertEqual(t, tc.expCls,
					tc.cfg.Storage.Tiers.BdevConfigs()[0].Class,
					"unexpected bdev class")

				ecp = filepath.Join(tc.cfg.Storage.Tiers.ScmConfigs()[0].Scm.MountPoint,
					storage.BdevOutConfName)
			}
			test.AssertEqual(t, ecp, tc.cfg.Storage.ConfigOutputPath,
				"unexpected config path")
		})
	}
}

func TestConfig_Validation(t *testing.T) {
	validConfig := func() *Config {
		return MockConfig().WithFabricProvider("foo").
			WithFabricInterface("ib0").
			WithFabricInterfacePort(42).
			WithStorage(
				storage.NewTierConfig().
					WithStorageClass("ram").
					WithScmRamdiskSize(storage.MinRamdiskMem).
					WithScmMountPoint("/foo/bar"),
			).
			WithTargetCount(8).
			WithPinnedNumaNode(0)
	}

	for name, tc := range map[string]struct {
		cfg    *Config
		expErr error
	}{
		"empty config should fail": {
			cfg:    MockConfig(),
			expErr: errors.New("target count must be nonzero"),
		},
		"config with pinned_numa_node and nonzero first_core should fail": {
			cfg:    validConfig().WithPinnedNumaNode(1).WithServiceThreadCore(1),
			expErr: errors.New("cannot specify both"),
		},
		"config with negative target count should fail": {
			cfg:    validConfig().WithTargetCount(-10),
			expErr: errors.New("must not be negative"),
		},
		"config with negative helper stream count should fail": {
			cfg:    validConfig().WithHelperStreamCount(-10),
			expErr: errors.New("must not be negative"),
		},
		"config with negative service core index should fail": {
			cfg: func() *Config {
				c := validConfig().WithServiceThreadCore(-10)
				c.PinnedNumaNode = nil
				return c
			}(),
			expErr: errors.New("must not be negative"),
		},
		"config with negative memory size should fail": {
			cfg:    validConfig().WithMemSize(-10),
			expErr: errors.New("must not be negative"),
		},
		"config with negative hugepage size should fail": {
			cfg:    validConfig().WithHugepageSize(-10),
			expErr: errors.New("must not be negative"),
		},
		"config with zero target count should fail": {
			cfg:    validConfig().WithTargetCount(0),
			expErr: errors.New("target count must be nonzero"),
		},
		"minimally-valid config should pass": {
			cfg: validConfig(),
		},
		"invalid log mask in config": {
			cfg:    validConfig().WithLogMask("DBGG"),
			expErr: errUnknownLogLevel("DBGG"),
		},
		"empty DD_MASK env in config": {
			cfg: validConfig().WithEnvVars("DD_MASK="),
		},
		"empty DD_SUBSYS env in config": {
			cfg: validConfig().WithEnvVars("DD_SUBSYS="),
		},
		"invalid DD_MASK env in config": {
			cfg:    validConfig().WithEnvVars("DD_MASK=mgmt,grogu"),
			expErr: errors.New("unknown name \"grogu\""),
		},
		"invalid DD_SUBSYS env in config": {
			cfg:    validConfig().WithEnvVars("DD_SUBSYS=mgmt,mando"),
			expErr: errors.New("unknown name \"mando\""),
		},
		"valid DD_MASK env in config": {
			cfg: validConfig().WithEnvVars("DD_MASK=REBUILD,PL,mgmt,epc"),
		},
		"valid DD_SUBSYS env in config": {
			cfg: validConfig().WithEnvVars("DD_SUBSYS=COMMON,misc,rpc"),
		},
		"valid 'all' DD_MASKs in config": {
			cfg: validConfig().WithEnvVars("DD_MASK=all"),
		},
		"valid 'all' DD_SUBSYS in config": {
			cfg: validConfig().WithEnvVars("DD_SUBSYS=all"),
		},
		"invalid 'all' with another debug stream in config": {
			cfg:    validConfig().WithEnvVars("DD_MASK=all,PL"),
			expErr: errLogNameAllWithOther,
		},
		"invalid 'all' with another subsystem in config": {
			cfg:    validConfig().WithEnvVars("DD_SUBSYS=all,MEM"),
			expErr: errLogNameAllWithOther,
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.CmpErr(t, tc.expErr, tc.cfg.Validate())
		})
	}
}

func TestConfig_FabricValidation(t *testing.T) {
	for name, tc := range map[string]struct {
		cfg    FabricConfig
		expErr error
	}{
		"missing provider": {
			cfg: FabricConfig{
				Interface:     "bar",
				InterfacePort: 42,
			},
			expErr: errors.New("provider"),
		},
		"missing interface": {
			cfg: FabricConfig{
				Provider:      "foo",
				InterfacePort: 42,
			},
			expErr: errors.New("fabric_iface"),
		},
		"missing port": {
			cfg: FabricConfig{
				Provider:  "foo",
				Interface: "bar",
			},
			expErr: errors.New("fabric_iface_port"),
		},
		"negative port number": {
			cfg: FabricConfig{
				Provider:      "foo",
				Interface:     "bar",
				InterfacePort: -42,
			},
			expErr: errors.New("fabric_iface_port"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotErr := tc.cfg.Validate()
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestConfig_ToCmdVals(t *testing.T) {
	var (
		mountPoint      = "/mnt/test"
		provider        = "test+foo"
		interfaceName   = "ib0"
		modules         = "foo,bar,baz"
		systemName      = "test-system"
		socketDir       = "/var/run/foo"
		logMask         = "LOG_MASK_VALUE"
		logFile         = "/path/to/log"
		cfgPath         = "/path/to/nvme.conf"
		interfacePort   = 20
		targetCount     = 4
		helperCount     = 1
		serviceCore     = 8
		index           = 2
		pinnedNumaNode  = uint(1)
		bypass          = true
		crtCtxShareAddr = uint32(1)
		crtTimeout      = uint32(30)
		memSize         = 8192
		hugepageSz      = 2
	)
	cfg := MockConfig().
		WithStorage(
			storage.NewTierConfig().
				WithScmMountPoint(mountPoint),
		).
		WithStorageConfigOutputPath(cfgPath).
		WithTargetCount(targetCount).
		WithHelperStreamCount(helperCount).
		WithServiceThreadCore(serviceCore).
		WithFabricProvider(provider).
		WithFabricInterface(interfaceName).
		WithFabricInterfacePort(interfacePort).
		WithPinnedNumaNode(pinnedNumaNode).
		WithBypassHealthChk(&bypass).
		WithModules(modules).
		WithSocketDir(socketDir).
		WithLogFile(logFile).
		WithLogMask(logMask).
		WithSystemName(systemName).
		WithCrtCtxShareAddr(crtCtxShareAddr).
		WithCrtTimeout(crtTimeout).
		WithMemSize(memSize).
		WithHugepageSize(hugepageSz).
		WithSrxDisabled(true)

	cfg.Index = uint32(index)

	wantArgs := []string{
		"-x", strconv.Itoa(helperCount),
		"-t", strconv.Itoa(targetCount),
		"-s", mountPoint,
		"-m", modules,
		"-f", strconv.Itoa(serviceCore),
		"-g", systemName,
		"-d", socketDir,
		"-n", cfgPath,
		"-I", strconv.Itoa(index),
		"-T", strconv.Itoa(len(cfg.Storage.Tiers)),
		"-p", strconv.FormatUint(uint64(pinnedNumaNode), 10),
		"-b",
		"-r", strconv.Itoa(memSize),
		"-H", strconv.Itoa(hugepageSz),
	}
	wantEnv := []string{
		"OFI_INTERFACE=" + interfaceName,
		"OFI_PORT=" + strconv.Itoa(interfacePort),
		"CRT_PHY_ADDR_STR=" + provider,
		"D_LOG_FILE=" + logFile,
		"D_LOG_MASK=" + logMask,
		"CRT_TIMEOUT=" + strconv.FormatUint(uint64(crtTimeout), 10),
		"CRT_CTX_SHARE_ADDR=" + strconv.FormatUint(uint64(crtCtxShareAddr), 10),
		"FI_OFI_RXM_USE_SRX=0",
	}

	gotArgs, err := cfg.CmdLineArgs()
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(wantArgs, gotArgs, defConfigCmpOpts...); diff != "" {
		t.Fatalf("(-want, +got):\n%s", diff)
	}

	gotEnv, err := cfg.CmdLineEnv()
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(wantEnv, gotEnv, defConfigCmpOpts...); diff != "" {
		t.Fatalf("(-want, +got):\n%s", diff)
	}
}

func TestConfig_EnvVarConflict(t *testing.T) {
	logMask1 := "LOG_MASK_VALUE_1"
	logMask2 := "LOG_MASK_VALUE_2"

	for name, tc := range map[string]struct {
		logMask    string
		envLogMask string
		expEnvMask string
	}{
		"log_mask takes precedence": {
			logMask:    logMask1,
			envLogMask: logMask2,
			expEnvMask: logMask1,
		},
		"empty log_mask uses env": {
			logMask:    "",
			envLogMask: logMask2,
			expEnvMask: logMask2,
		},
	} {
		t.Run(name, func(t *testing.T) {
			cfg := MockConfig().
				WithLogMask(tc.logMask).
				WithEnvVars("D_LOG_MASK=" + tc.envLogMask)

			wantEnv := []string{
				"D_LOG_MASK=" + tc.expEnvMask,
				"CRT_TIMEOUT=0",
				"CRT_CTX_SHARE_ADDR=0",
				"FI_OFI_RXM_USE_SRX=1",
			}

			gotEnv, err := cfg.CmdLineEnv()
			if err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(wantEnv, gotEnv, defConfigCmpOpts...); diff != "" {
				t.Fatalf("(-want, +got):\n%s", diff)
			}
		})
	}
}

func TestFabricConfig_Update(t *testing.T) {
	for name, tc := range map[string]struct {
		fc        *FabricConfig
		new       FabricConfig
		expResult *FabricConfig
	}{
		"nil": {},
		"nothing set": {
			fc:        &FabricConfig{},
			new:       FabricConfig{},
			expResult: &FabricConfig{},
		},
		"update": {
			fc: &FabricConfig{},
			new: FabricConfig{
				Provider:        "provider",
				Interface:       "iface",
				InterfacePort:   9999,
				CrtCtxShareAddr: 2,
				CrtTimeout:      60,
				DisableSRX:      true,
			},
			expResult: &FabricConfig{
				Provider:        "provider",
				Interface:       "iface",
				InterfacePort:   9999,
				CrtCtxShareAddr: 2,
				CrtTimeout:      60,
				DisableSRX:      true,
			},
		},
		"don't unset fields": {
			fc: &FabricConfig{
				Provider:        "provider",
				Interface:       "iface",
				InterfacePort:   9999,
				CrtCtxShareAddr: 2,
				CrtTimeout:      60,
				DisableSRX:      true,
			},
			new: FabricConfig{},
			expResult: &FabricConfig{
				Provider:        "provider",
				Interface:       "iface",
				InterfacePort:   9999,
				CrtCtxShareAddr: 2,
				CrtTimeout:      60,
				DisableSRX:      true,
			},
		},
		"update mixed": {
			fc: &FabricConfig{
				CrtCtxShareAddr: 2,
				CrtTimeout:      60,
			},
			new: FabricConfig{
				Provider:        "provider",
				Interface:       "iface",
				InterfacePort:   9999,
				CrtCtxShareAddr: 15,
				CrtTimeout:      120,
				DisableSRX:      true,
			},
			expResult: &FabricConfig{
				Provider:        "provider",
				Interface:       "iface",
				InterfacePort:   9999,
				CrtCtxShareAddr: 2,
				CrtTimeout:      60,
				DisableSRX:      true,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			tc.fc.Update(tc.new)

			if diff := cmp.Diff(tc.expResult, tc.fc); diff != "" {
				t.Fatalf("(-want, +got):\n%s", diff)
			}
		})
	}
}
