//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package engine

import (
	"flag"
	"os"
	"path/filepath"
	"strconv"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"
	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/server/storage"
)

var update = flag.Bool("update", false, "update .golden files")

func cmpOpts() []cmp.Option {
	return []cmp.Option{
		cmpopts.SortSlices(func(a, b string) bool { return a < b }),
	}
}

func TestConfig_MergeEnvVars(t *testing.T) {
	for name, tc := range map[string]struct {
		baseVars  []string
		mergeVars []string
		wantVars  []string
	}{
		"no dupes without merge": {
			baseVars:  []string{"FOO=BAR", "FOO=BAZ"},
			mergeVars: []string{},
			wantVars:  []string{"FOO=BAR"},
		},
		"no dupes after merge": {
			baseVars:  []string{"FOO=BAR", "FOO=BAZ"},
			mergeVars: []string{"FOO=QUX"},
			wantVars:  []string{"FOO=QUX"},
		},
		"no dupes in merge": {
			baseVars:  []string{"FOO=BAR"},
			mergeVars: []string{"FOO=BAZ", "FOO=QUX"},
			wantVars:  []string{"FOO=BAZ"},
		},
		"basic test": {
			baseVars:  []string{"A=B"},
			mergeVars: []string{"C=D"},
			wantVars:  []string{"A=B", "C=D"},
		},
		"complex value": {
			baseVars:  []string{"SIMPLE=OK"},
			mergeVars: []string{"COMPLEX=FOO;bar=quux;woof=meow"},
			wantVars:  []string{"SIMPLE=OK", "COMPLEX=FOO;bar=quux;woof=meow"},
		},
		"append no base": {
			baseVars:  []string{},
			mergeVars: []string{"C=D"},
			wantVars:  []string{"C=D"},
		},
		"skip malformed": {
			baseVars:  []string{"GOOD_BASE=OK", "BAD_BASE="},
			mergeVars: []string{"GOOD_MERGE=OK", "BAD_MERGE"},
			wantVars:  []string{"GOOD_BASE=OK", "GOOD_MERGE=OK"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotVars := mergeEnvVars(tc.baseVars, tc.mergeVars)
			if diff := cmp.Diff(tc.wantVars, gotVars, cmpOpts()...); diff != "" {
				t.Fatalf("(-want, +got):\n%s", diff)
			}
		})
	}
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
			cfg := NewConfig().
				WithEnvVars(tc.startVars...)

			if !cfg.HasEnvVar(tc.addVar) {
				cfg.WithEnvVars(tc.addVar + "=" + tc.addVal)
			}

			if diff := cmp.Diff(tc.expVars, cfg.EnvVars, cmpOpts()...); diff != "" {
				t.Fatalf("unexpected env vars:\n%s\n", diff)
			}
		})
	}
}

func TestConstructedConfig(t *testing.T) {
	var numaNode uint = 8
	goldenPath := "testdata/full.golden"

	// just set all values regardless of validity
	constructed := NewConfig().
		WithRank(37).
		WithFabricProvider("foo+bar").
		WithFabricInterface("qib42").
		WithFabricInterfacePort(100).
		WithModules("foo,bar,baz").
		WithStorage(
			storage.NewTierConfig().
				WithScmClass("ram").
				WithScmRamdiskSize(42).
				WithScmMountPoint("/mnt/daostest").
				WithScmDeviceList("/dev/a", "/dev/b"),
			storage.NewTierConfig().
				WithBdevClass("kdev").
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
		WithPinnedNumaNode(&numaNode).
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

	if diff := cmp.Diff(fromDisk, constructed, cmpOpts()...); diff != "" {
		t.Fatalf("(-want, +got):\n%s", diff)
	}
}

func TestConfig_ScmValidation(t *testing.T) {
	baseValidConfig := func() *Config {
		return NewConfig().
			WithFabricProvider("test"). // valid enough to pass "not-blank" test
			WithFabricInterface("test").
			WithFabricInterfacePort(42)
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
			expErr: errors.New("no storage class"),
		},
		"missing scm_mount": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithScmClass("ram"),
				),
			expErr: errors.New("scm_mount"),
		},
		"ramdisk valid": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithScmClass("ram").
						WithScmRamdiskSize(1).
						WithScmMountPoint("test"),
				),
		},
		"ramdisk missing scm_size": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithScmClass("ram").
						WithScmMountPoint("test"),
				),
			expErr: errors.New("scm_size"),
		},
		"ramdisk scm_size: 0": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithScmClass("ram").
						WithScmRamdiskSize(0).
						WithScmMountPoint("test"),
				),
			expErr: errors.New("scm_size"),
		},
		"ramdisk with scm_list": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithScmClass("ram").
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
						WithScmClass("dcpm").
						WithScmDeviceList("foo").
						WithScmMountPoint("test"),
				),
		},
		"dcpm scm_list too long": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithScmClass("dcpm").
						WithScmDeviceList("foo", "bar").
						WithScmMountPoint("test"),
				),
			expErr: errors.New("scm_list"),
		},
		"dcpm scm_list empty": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithScmClass("dcpm").
						WithScmMountPoint("test"),
				),
			expErr: errors.New("scm_list"),
		},
		"dcpm with scm_size": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithScmClass("dcpm").
						WithScmDeviceList("foo").
						WithScmRamdiskSize(1).
						WithScmMountPoint("test"),
				),
			expErr: errors.New("scm_size"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			common.CmpErr(t, tc.expErr, tc.cfg.Validate())
		})
	}
}

func TestConfig_BdevValidation(t *testing.T) {
	baseValidConfig := func() *Config {
		return NewConfig().
			WithFabricProvider("test"). // valid enough to pass "not-blank" test
			WithFabricInterface("test").
			WithFabricInterfacePort(42).
			WithStorage(
				storage.NewTierConfig().
					WithScmClass("dcpm").
					WithScmDeviceList("foo").
					WithScmMountPoint("test"),
			)
	}

	for name, tc := range map[string]struct {
		cfg             *Config
		expErr          error
		expCls          storage.Class
		expEmptyCfgPath bool
	}{
		"unknown class": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithBdevClass("nvmed"),
				),
			expErr: errors.New("no storage class"),
		},
		"nvme class; no devices": {
			// output config path should be empty and the empty tier removed
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithBdevClass("nvme"),
				),
			expEmptyCfgPath: true,
		},
		"nvme class; good pci addresses": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithBdevClass("nvme").
						WithBdevDeviceList(common.MockPCIAddr(1), common.MockPCIAddr(2)),
				),
		},
		"nvme class; duplicate pci address": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithBdevClass("nvme").
						WithBdevDeviceList(common.MockPCIAddr(1), common.MockPCIAddr(1)),
				),
			expErr: errors.New("bdev_list"),
		},
		"nvme class; bad pci address": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithBdevClass("nvme").
						WithBdevDeviceList(common.MockPCIAddr(1), "0000:00:00"),
				),
			expErr: errors.New("unexpected pci address"),
		},
		"kdev class; no devices": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithBdevClass("kdev"),
				),
			expErr: errors.New("kdev requires non-empty bdev_list"),
		},
		"kdev class; valid": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithBdevClass("kdev").
						WithBdevDeviceList("/dev/sda"),
				),
			expCls: storage.ClassKdev,
		},
		"file class; no size": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithBdevClass("file").
						WithBdevDeviceList("bdev1"),
				),
			expErr: errors.New("file requires non-zero bdev_size"),
		},
		"file class; negative size": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithBdevClass("file").
						WithBdevDeviceList("bdev1").
						WithBdevFileSize(-1),
				),
			expErr: errors.New("negative bdev_size"),
		},
		"file class; no devices": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithBdevClass("file").
						WithBdevFileSize(10),
				),
			expErr: errors.New("file requires non-empty bdev_list"),
		},
		"file class; valid": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithBdevClass("file").
						WithBdevFileSize(10).
						WithBdevDeviceList("bdev1", "bdev2"),
				),
			expCls: storage.ClassFile,
		},
	} {
		t.Run(name, func(t *testing.T) {
			common.CmpErr(t, tc.expErr, tc.cfg.Validate())
			if tc.expErr != nil {
				return
			}

			var ecp string
			if !tc.expEmptyCfgPath {
				if tc.expCls == "" {
					tc.expCls = storage.ClassNvme // default if unset
				}
				common.AssertEqual(t, tc.expCls,
					tc.cfg.Storage.Tiers.BdevConfigs()[0].Class,
					"unexpected bdev class")

				ecp = filepath.Join(tc.cfg.Storage.Tiers.ScmConfigs()[0].Scm.MountPoint,
					storage.BdevOutConfName)
			}
			common.AssertEqual(t, ecp, tc.cfg.Storage.ConfigOutputPath,
				"unexpected config path")
		})
	}
}

func TestConfig_Validation(t *testing.T) {
	bad := NewConfig()

	if err := bad.Validate(); err == nil {
		t.Fatal("expected empty config to fail validation")
	}

	// create a minimally-valid config
	good := NewConfig().WithFabricProvider("foo").
		WithFabricInterface("qib0").
		WithFabricInterfacePort(42).
		WithStorage(
			storage.NewTierConfig().
				WithScmClass("ram").
				WithScmRamdiskSize(1).
				WithScmMountPoint("/foo/bar"),
		)

	if err := good.Validate(); err != nil {
		t.Fatalf("expected %#v to validate; got %s", good, err)
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
			common.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestConfig_ToCmdVals(t *testing.T) {
	var (
		mountPoint      = "/mnt/test"
		provider        = "test+foo"
		interfaceName   = "qib0"
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
	cfg := NewConfig().
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
		WithPinnedNumaNode(&pinnedNumaNode).
		WithBypassHealthChk(&bypass).
		WithModules(modules).
		WithSocketDir(socketDir).
		WithLogFile(logFile).
		WithLogMask(logMask).
		WithSystemName(systemName).
		WithCrtCtxShareAddr(crtCtxShareAddr).
		WithCrtTimeout(crtTimeout).
		WithMemSize(memSize).
		WithHugePageSize(hugepageSz)

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
	}

	gotArgs, err := cfg.CmdLineArgs()
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(wantArgs, gotArgs, cmpOpts()...); diff != "" {
		t.Fatalf("(-want, +got):\n%s", diff)
	}

	gotEnv, err := cfg.CmdLineEnv()
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(wantEnv, gotEnv, cmpOpts()...); diff != "" {
		t.Fatalf("(-want, +got):\n%s", diff)
	}
}
