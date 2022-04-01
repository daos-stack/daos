//
// (C) Copyright 2019-2022 Intel Corporation.
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
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"
	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
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
			if diff := cmp.Diff(tc.wantVars, gotVars, defConfigCmpOpts...); diff != "" {
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
				common.AssertTrue(t, tc.expErr != nil,
					fmt.Sprintf("Unexpected error %q", err))
				common.CmpErr(t, tc.expErr, err)
				common.AssertEqual(t, value, "",
					fmt.Sprintf("Unexpected value %q for key %q",
						tc.key, value))
				return
			}

			common.AssertTrue(t, tc.expErr == nil,
				fmt.Sprintf("Expected error %q", tc.expErr))
			common.AssertEqual(t, value, tc.expValue, "Invalid value returned")
		})
	}
}

func TestConfig_Constructed(t *testing.T) {
	goldenPath := "testdata/full.golden"

	// just set all values regardless of validity
	constructed := MockConfig().
		WithRank(37).
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
			expErr: errors.New("no storage class"),
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
						WithScmRamdiskSize(1).
						WithScmMountPoint("test"),
				),
		},
		"ramdisk missing scm_size": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("ram").
						WithScmMountPoint("test"),
				),
			expErr: errors.New("scm_size"),
		},
		"ramdisk scm_size: 0": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("ram").
						WithScmRamdiskSize(0).
						WithScmMountPoint("test"),
				),
			expErr: errors.New("scm_size"),
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
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			common.CmpErr(t, tc.expErr, tc.cfg.Validate(log, nil))
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
			WithPinnedNumaNode(0)
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
						WithStorageClass("nvmed"),
				),
			expErr: errors.New("no storage class"),
		},
		"nvme class; no devices": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("nvme"),
				),
			expErr: errors.New("valid PCI addresses"),
		},
		"nvme class; good pci addresses": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("nvme").
						WithBdevDeviceList(common.MockPCIAddr(1), common.MockPCIAddr(2)),
				),
		},
		"nvme class; duplicate pci address": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("nvme").
						WithBdevDeviceList(common.MockPCIAddr(1), common.MockPCIAddr(1)),
				),
			expErr: errors.New("bdev_list"),
		},
		"nvme class; bad pci address": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("nvme").
						WithBdevDeviceList(common.MockPCIAddr(1), "0000:00:00"),
				),
			expErr: errors.New("valid PCI addresses"),
		},
		"kdev class; no devices": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("kdev"),
				),
			expErr: errors.New("kdev requires non-empty bdev_list"),
		},
		"kdev class; valid": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("kdev").
						WithBdevDeviceList("/dev/sda"),
				),
			expCls: storage.ClassKdev,
		},
		"file class; no size": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("file").
						WithBdevDeviceList("bdev1"),
				),
			expErr: errors.New("file requires non-zero bdev_size"),
		},
		"file class; negative size": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("file").
						WithBdevDeviceList("bdev1").
						WithBdevFileSize(-1),
				),
			expErr: errors.New("negative bdev_size"),
		},
		"file class; no devices": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("file").
						WithBdevFileSize(10),
				),
			expErr: errors.New("file requires non-empty bdev_list"),
		},
		"file class; valid": {
			cfg: baseValidConfig().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("file").
						WithBdevFileSize(10).
						WithBdevDeviceList("bdev1", "bdev2"),
				),
			expCls: storage.ClassFile,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			common.CmpErr(t, tc.expErr, tc.cfg.Validate(log, nil))
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
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	bad := MockConfig()

	if err := bad.Validate(log, nil); err == nil {
		t.Fatal("expected empty config to fail validation")
	}

	// create a minimally-valid config
	good := MockConfig().WithFabricProvider("foo").
		WithFabricInterface("ib0").
		WithFabricInterfacePort(42).
		WithStorage(
			storage.NewTierConfig().
				WithStorageClass("ram").
				WithScmRamdiskSize(1).
				WithScmMountPoint("/foo/bar"),
		).
		WithPinnedNumaNode(0)

	if err := good.Validate(log, nil); err != nil {
		t.Fatalf("expected %#v to validate; got %s", good, err)
	}
}

func multiProviderString(comp ...string) string {
	return strings.Join(comp, MultiProviderSeparator)
}

func TestConfig_FabricValidation(t *testing.T) {
	for name, tc := range map[string]struct {
		cfg    FabricConfig
		expErr error
	}{
		"missing provider": {
			cfg: FabricConfig{
				Interface:     "bar",
				InterfacePort: "42",
			},
			expErr: errors.New("provider"),
		},
		"missing interface": {
			cfg: FabricConfig{
				Provider:      "foo",
				InterfacePort: "42",
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
				InterfacePort: "-42",
			},
			expErr: errors.New("fabric_iface_port"),
		},
		"success": {
			cfg: FabricConfig{
				Provider:      "foo",
				Interface:     "bar",
				InterfacePort: "42",
			},
		},
		"multi provider/interface/port ok": {
			cfg: FabricConfig{
				Provider:      multiProviderString("foo", "bar"),
				Interface:     multiProviderString("baz", "net"),
				InterfacePort: multiProviderString("42", "128"),
			},
		},
		"mismatched num providers": {
			cfg: FabricConfig{
				Provider:      "foo",
				Interface:     multiProviderString("baz", "net"),
				InterfacePort: multiProviderString("42", "128"),
			},
			expErr: errors.New("same number"),
		},
		"mismatched num interfaces": {
			cfg: FabricConfig{
				Provider:      multiProviderString("foo", "bar"),
				Interface:     "baz",
				InterfacePort: multiProviderString("42", "128"),
			},
			expErr: errors.New("same number"),
		},
		"mismatched num ports": {
			cfg: FabricConfig{
				Provider:      multiProviderString("foo", "bar"),
				Interface:     multiProviderString("baz", "net"),
				InterfacePort: "42",
			},
			expErr: errors.New("same number"),
		},
		"nr secondary ctxs less than 1": {
			cfg: FabricConfig{
				Provider:              multiProviderString("foo", "bar"),
				Interface:             multiProviderString("baz", "net"),
				InterfacePort:         multiProviderString("42", "128"),
				NumSecondaryEndpoints: []int{0},
			},
			expErr: errors.New("must be > 0"),
		},
		"nr secondary ctxs okay": {
			cfg: FabricConfig{
				Provider:              multiProviderString("foo", "bar", "baz"),
				Interface:             multiProviderString("net0", "net1", "net2"),
				InterfacePort:         multiProviderString("42", "128", "256"),
				NumSecondaryEndpoints: []int{1, 2},
			},
		},
		"too many nr secondary ctxs": {
			cfg: FabricConfig{
				Provider:              multiProviderString("foo", "bar", "baz"),
				Interface:             multiProviderString("net0", "net1", "net2"),
				InterfacePort:         multiProviderString("42", "128", "256"),
				NumSecondaryEndpoints: []int{1, 2, 3},
			},
			expErr: errors.New("must have one value for each"),
		},
		"too few nr secondary ctxs": {
			cfg: FabricConfig{
				Provider:              multiProviderString("foo", "bar", "baz"),
				Interface:             multiProviderString("net0", "net1", "net2"),
				InterfacePort:         multiProviderString("42", "128", "256"),
				NumSecondaryEndpoints: []int{1},
			},
			expErr: errors.New("must have one value for each"),
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
		"FI_OFI_RXM_USE_SRX=1",
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

func TestConfig_setAffinity(t *testing.T) {
	for name, tc := range map[string]struct {
		cfg     *Config
		fi      *hardware.FabricInterface
		expErr  error
		expNuma uint
	}{
		"numa pinned; matching iface": {
			cfg: MockConfig().
				WithPinnedNumaNode(1).
				WithFabricInterface("ib1").
				WithFabricProvider("ofi+verbs"),
			fi: &hardware.FabricInterface{
				Name:         "ib1",
				NetInterface: "ib1",
				NUMANode:     1,
				Providers:    common.NewStringSet("ofi+verbs"),
			},
			expNuma: 1,
		},
		// NOTE: this currently logs an error but could instead return one
		//       but there might be legitimate use cases e.g. sharing interface
		"numa pinned; not matching iface": {
			cfg: MockConfig().
				WithPinnedNumaNode(1).
				WithFabricInterface("ib2").
				WithFabricProvider("ofi+verbs"),
			fi: &hardware.FabricInterface{
				Name:         "ib2",
				NetInterface: "ib2",
				NUMANode:     2,
				Providers:    common.NewStringSet("ofi+verbs"),
			},
			expNuma: 1,
		},
		"numa not pinned": {
			cfg: MockConfig().
				WithFabricInterface("ib1").
				WithFabricProvider("ofi+verbs"),
			fi: &hardware.FabricInterface{
				Name:         "ib1",
				NetInterface: "ib1",
				NUMANode:     1,
				Providers:    common.NewStringSet("ofi+verbs"),
			},
			expNuma: 1,
		},
		"validation success": {
			cfg: MockConfig().
				WithFabricInterface("net1").
				WithFabricProvider("test").
				WithPinnedNumaNode(1),
			fi: &hardware.FabricInterface{
				Name:         "net1",
				NetInterface: "net1",
				Providers:    common.NewStringSet("test"),
			},
			expNuma: 1,
		},
		"provider not supported": {
			cfg: MockConfig().
				WithFabricInterface("net1").
				WithFabricProvider("test").
				WithPinnedNumaNode(1),
			fi: &hardware.FabricInterface{
				Name:         "net1",
				NetInterface: "net1",
				Providers:    common.NewStringSet("test2"),
			},
			expErr: errors.New("not supported"),
		},
		"no fabric info; pinned numa": {
			cfg: MockConfig().
				WithFabricInterface("net1").
				WithFabricProvider("test").
				WithPinnedNumaNode(1),
			expNuma: 1,
		},
		"no fabric info; no pinned numa": {
			cfg: MockConfig().
				WithFabricInterface("net1").
				WithFabricProvider("test"),
			expErr: errors.New("fabric info not provided"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			var fis *hardware.FabricInterfaceSet
			if tc.fi != nil {
				fis = hardware.NewFabricInterfaceSet(tc.fi)
			}

			err := tc.cfg.setAffinity(log, fis)
			common.CmpErr(t, tc.expErr, err)

			common.AssertEqual(t, tc.expNuma, tc.cfg.Storage.NumaNodeIndex,
				"unexpected storage numa node id")
			common.AssertEqual(t, tc.expNuma, tc.cfg.Fabric.NumaNodeIndex,
				"unexpected fabric numa node id")
		})
	}
}

func TestFabricConfig_GetProviders(t *testing.T) {
	for name, tc := range map[string]struct {
		cfg          *FabricConfig
		expProviders []string
		expErr       error
	}{
		"nil": {
			expErr: errors.New("nil"),
		},
		"empty": {
			cfg:    &FabricConfig{},
			expErr: errors.New("provider not set"),
		},
		"single": {
			cfg: &FabricConfig{
				Provider: "p1",
			},
			expProviders: []string{"p1"},
		},
		"multi": {
			cfg: &FabricConfig{
				Provider: multiProviderString("p1", "p2", "p3"),
			},
			expProviders: []string{"p1", "p2", "p3"},
		},
		"excessive whitespace": {
			cfg: &FabricConfig{
				Provider: multiProviderString(" ", " p1 ", "  p2 ", "p3"),
			},
			expProviders: []string{"p1", "p2", "p3"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			providers, err := tc.cfg.GetProviders()

			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expProviders, providers); diff != "" {
				t.Fatalf("(-want, +got):\n%s", diff)
			}
		})
	}
}

func TestFabricConfig_GetNumProviders(t *testing.T) {
	for name, tc := range map[string]struct {
		cfg    *FabricConfig
		expNum int
	}{
		"nil": {},
		"empty": {
			cfg: &FabricConfig{},
		},
		"single": {
			cfg: &FabricConfig{
				Provider: "p1",
			},
			expNum: 1,
		},
		"multi": {
			cfg: &FabricConfig{
				Provider: multiProviderString("p1", "p2", "p3", "p4"),
			},
			expNum: 4,
		},
	} {
		t.Run(name, func(t *testing.T) {
			common.AssertEqual(t, tc.expNum, tc.cfg.GetNumProviders(), "")
		})
	}
}

func TestFabricConfig_GetPrimaryProvider(t *testing.T) {
	for name, tc := range map[string]struct {
		cfg         *FabricConfig
		expProvider string
		expErr      error
	}{
		"nil": {
			expErr: errors.New("nil"),
		},
		"empty": {
			cfg:    &FabricConfig{},
			expErr: errors.New("provider not set"),
		},
		"single": {
			cfg: &FabricConfig{
				Provider: "p1",
			},
			expProvider: "p1",
		},
		"multi": {
			cfg: &FabricConfig{
				Provider: multiProviderString("p1", "p2", "p3"),
			},
			expProvider: "p1",
		},
	} {
		t.Run(name, func(t *testing.T) {
			provider, err := tc.cfg.GetPrimaryProvider()

			common.CmpErr(t, tc.expErr, err)
			common.AssertEqual(t, tc.expProvider, provider, "")
		})
	}
}

func TestFabricConfig_GetInterfaces(t *testing.T) {
	for name, tc := range map[string]struct {
		cfg           *FabricConfig
		expInterfaces []string
		expErr        error
	}{
		"nil": {
			expErr: errors.New("nil"),
		},
		"empty": {
			cfg:    &FabricConfig{},
			expErr: errors.New("fabric_iface not set"),
		},
		"single": {
			cfg: &FabricConfig{
				Interface: "net1",
			},
			expInterfaces: []string{"net1"},
		},
		"multi": {
			cfg: &FabricConfig{
				Interface: multiProviderString("net1", "net2", "net3"),
			},
			expInterfaces: []string{"net1", "net2", "net3"},
		},
		"excessive whitespace": {
			cfg: &FabricConfig{
				Interface: multiProviderString(" net1  ", "", "    net2", "net3", ""),
			},
			expInterfaces: []string{"net1", "net2", "net3"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			interfaces, err := tc.cfg.GetInterfaces()

			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expInterfaces, interfaces); diff != "" {
				t.Fatalf("(-want, +got):\n%s", diff)
			}
		})
	}
}

func TestFabricConfig_GetPrimaryInterface(t *testing.T) {
	for name, tc := range map[string]struct {
		cfg          *FabricConfig
		expInterface string
		expErr       error
	}{
		"nil": {
			expErr: errors.New("nil"),
		},
		"empty": {
			cfg:    &FabricConfig{},
			expErr: errors.New("fabric_iface not set"),
		},
		"single": {
			cfg: &FabricConfig{
				Interface: "net1",
			},
			expInterface: "net1",
		},
		"multi": {
			cfg: &FabricConfig{
				Interface: multiProviderString("net0", "net1", "net2", "net3"),
			},
			expInterface: "net0",
		},
	} {
		t.Run(name, func(t *testing.T) {
			iface, err := tc.cfg.GetPrimaryInterface()

			common.CmpErr(t, tc.expErr, err)
			common.AssertEqual(t, tc.expInterface, iface, "")
		})
	}
}

func TestFabricConfig_GetInterfacePorts(t *testing.T) {
	for name, tc := range map[string]struct {
		cfg      *FabricConfig
		expPorts []int
		expErr   error
	}{
		"nil": {
			expErr: errors.New("nil"),
		},
		"empty": {
			cfg:    &FabricConfig{},
			expErr: errors.New("fabric_iface_port not set"),
		},
		"single": {
			cfg: &FabricConfig{
				InterfacePort: "1234",
			},
			expPorts: []int{1234},
		},
		"multi": {
			cfg: &FabricConfig{
				InterfacePort: multiProviderString("1234", "5678", "9012"),
			},
			expPorts: []int{1234, 5678, 9012},
		},
		"excessive whitespace": {
			cfg: &FabricConfig{
				InterfacePort: multiProviderString("1234   ", "  5678  ", "", " 9012"),
			},
			expPorts: []int{1234, 5678, 9012},
		},
		"non-integer port": {
			cfg: &FabricConfig{
				InterfacePort: multiProviderString("1234", "a123"),
			},
			expErr: errors.New("strconv.Atoi"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			ports, err := tc.cfg.GetInterfacePorts()

			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expPorts, ports); diff != "" {
				t.Fatalf("(-want, +got):\n%s", diff)
			}
		})
	}
}

func TestFabricConfig_Update(t *testing.T) {
	for name, tc := range map[string]struct {
		fc        *FabricConfig
		other     FabricConfig
		expResult *FabricConfig
	}{
		"set all": {
			fc: &FabricConfig{},
			other: FabricConfig{
				Provider:              "p",
				Interface:             "i",
				InterfacePort:         "1234",
				CrtCtxShareAddr:       2,
				CrtTimeout:            3,
				NumSecondaryEndpoints: []int{1},
			},
			expResult: &FabricConfig{
				Provider:              "p",
				Interface:             "i",
				InterfacePort:         "1234",
				CrtCtxShareAddr:       2,
				CrtTimeout:            3,
				NumSecondaryEndpoints: []int{1},
			},
		},
		"already set": {
			fc: &FabricConfig{
				Provider:              "p",
				Interface:             "i",
				InterfacePort:         "1234",
				CrtCtxShareAddr:       2,
				CrtTimeout:            3,
				NumSecondaryEndpoints: []int{1},
			},
			other: FabricConfig{
				Provider:              "q",
				Interface:             "h",
				InterfacePort:         "5678",
				CrtCtxShareAddr:       3,
				CrtTimeout:            4,
				NumSecondaryEndpoints: []int{5},
			},
			expResult: &FabricConfig{
				Provider:              "p",
				Interface:             "i",
				InterfacePort:         "1234",
				CrtCtxShareAddr:       2,
				CrtTimeout:            3,
				NumSecondaryEndpoints: []int{1},
			},
		},
		"default secondary ctx": {
			fc: &FabricConfig{},
			other: FabricConfig{
				Provider: multiProviderString("one", "two", "three"),
			},
			expResult: &FabricConfig{
				Provider:              multiProviderString("one", "two", "three"),
				NumSecondaryEndpoints: []int{1, 1},
			},
		},
		"no secondary ctx": {
			fc: &FabricConfig{},
			other: FabricConfig{
				Provider: "one",
			},
			expResult: &FabricConfig{
				Provider: "one",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			tc.fc.Update(tc.other)

			if diff := cmp.Diff(tc.expResult, tc.fc); diff != "" {
				t.Fatalf("(-want, +got):\n%s", diff)
			}
		})
	}
}
