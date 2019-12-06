//
// (C) Copyright 2019 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package ioserver

import (
	"flag"
	"os"
	"strconv"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"gopkg.in/yaml.v2"
)

var update = flag.Bool("update", false, "update .golden files")

func cmpOpts() []cmp.Option {
	return []cmp.Option{
		cmpopts.SortSlices(func(a, b string) bool { return a < b }),
	}
}

func TestMergeEnvVars(t *testing.T) {
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
			if diff := cmp.Diff(gotVars, tc.wantVars, cmpOpts()...); diff != "" {
				t.Fatalf("(-want, +got):\n%s", diff)
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
		WithSystemName("foo").
		WithSocketDir("/foo/bar").
		WithFabricProvider("foo+bar").
		WithFabricInterface("qib42").
		WithFabricInterfacePort(100).
		WithModules("foo,bar,baz").
		WithScmClass("ram").
		WithScmRamdiskSize(42).
		WithScmMountPoint("/mnt/daostest").
		WithScmDeviceList("/dev/a", "/dev/b").
		WithBdevClass("malloc").
		WithBdevDeviceCount(2).
		WithBdevFileSize(20).
		WithBdevDeviceList("/dev/c", "/dev/d").
		WithLogFile("/path/to/log").
		WithLogMask("DD_DEBUG").
		WithEnvVars("FOO=BAR", "BAZ=QUX").
		WithServiceThreadCore(8).
		WithTargetCount(12).
		WithHelperStreamCount(1).
		WithPinnedNumaNode(&numaNode)

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

	if diff := cmp.Diff(constructed, fromDisk, cmpOpts()...); diff != "" {
		t.Fatalf("(-want, +got):\n%s", diff)
	}
}

func TestConfigValidation(t *testing.T) {
	bad := NewConfig()

	if err := bad.Validate(); err == nil {
		t.Fatal("expected empty config to fail validation")
	}

	// create a minimally-valid config
	good := NewConfig().WithFabricProvider("foo").
		WithFabricInterface("qib0").
		WithScmClass("ram").
		WithScmMountPoint("/foo/bar")

	if err := good.Validate(); err != nil {
		t.Fatalf("expected %#v to validate; got %s", good, err)
	}
}

func TestConfigToCmdVals(t *testing.T) {
	var (
		mountPoint     = "/mnt/test"
		provider       = "test+foo"
		interfaceName  = "qib0"
		modules        = "foo,bar,baz"
		systemName     = "test-system"
		socketDir      = "/var/run/foo"
		logMask        = "LOG_MASK_VALUE"
		logFile        = "/path/to/log"
		cfgPath        = "/path/to/nvme.conf"
		shmId          = 42
		interfacePort  = 20
		targetCount    = 4
		helperCount    = 1
		serviceCore    = 8
		index          = 2
		pinnedNumaNode = uint(1)
	)
	cfg := NewConfig().
		WithScmMountPoint(mountPoint).
		WithTargetCount(targetCount).
		WithHelperStreamCount(helperCount).
		WithServiceThreadCore(serviceCore).
		WithFabricProvider(provider).
		WithFabricInterface(interfaceName).
		WithFabricInterfacePort(interfacePort).
		WithPinnedNumaNode(&pinnedNumaNode).
		WithModules(modules).
		WithSocketDir(socketDir).
		WithLogFile(logFile).
		WithLogMask(logMask).
		WithShmID(shmId).
		WithBdevConfigPath(cfgPath).
		WithSystemName(systemName)

	cfg.Index = uint32(index)

	wantArgs := []string{
		"-x", strconv.Itoa(helperCount),
		"-t", strconv.Itoa(targetCount),
		"-s", mountPoint,
		"-m", modules,
		"-f", strconv.Itoa(serviceCore),
		"-g", systemName,
		"-d", socketDir,
		"-i", strconv.Itoa(shmId),
		"-n", cfgPath,
		"-I", strconv.Itoa(index),
		"-p", strconv.FormatUint(uint64(pinnedNumaNode), 10),
	}
	wantEnv := []string{
		"OFI_INTERFACE=" + interfaceName,
		"OFI_PORT=" + strconv.Itoa(interfacePort),
		"CRT_PHY_ADDR_STR=" + provider,
		"D_LOG_FILE=" + logFile,
		"D_LOG_MASK=" + logMask,
	}

	gotArgs, err := cfg.CmdLineArgs()
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(gotArgs, wantArgs, cmpOpts()...); diff != "" {
		t.Fatalf("(-want, +got):\n%s", diff)
	}

	gotEnv, err := cfg.CmdLineEnv()
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(gotEnv, wantEnv, cmpOpts()...); diff != "" {
		t.Fatalf("(-want, +got):\n%s", diff)
	}
}
