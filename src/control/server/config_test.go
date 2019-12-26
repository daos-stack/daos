//
// (C) Copyright 2018-2019 Intel Corporation.
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

package server

import (
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	. "github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/ioserver"
)

const (
	sConfigUncomment = "daos_server_uncomment.yml"
	socketsExample   = "../../../utils/config/examples/daos_server_sockets.yml"
	psm2Example      = "../../../utils/config/examples/daos_server_psm2.yml"
	defaultConfig    = "../../../utils/config/daos_server.yml"
)

// uncommentServerConfig removes leading comment chars from daos_server.yml
// lines in order to verify parsing of all available params.
func uncommentServerConfig(t *testing.T, outFile string) {
	cmd := exec.Command(
		"bash", "-c", fmt.Sprintf("sed s/^#//g %s > %s", defaultConfig, outFile))

	stderr, err := cmd.StderrPipe()
	if err != nil {
		t.Fatal(err)
	}

	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}

	slurp, _ := ioutil.ReadAll(stderr)
	if string(slurp) != "" {
		t.Fatal(errors.New(string(slurp)))
	}

	if err := cmd.Wait(); err != nil {
		t.Fatal(err)
	}
}

// emptyMockConfig returns unpopulated configuration with a default mock
// external interfacing implementation.
func emptyMockConfig(t *testing.T) *Configuration {
	return newDefaultConfiguration(defaultMockExt())
}

// defaultMockConfig returns configuration populated from blank config file
// with mocked external interface.
func defaultMockConfig(t *testing.T) *Configuration {
	return mockConfigFromFile(t, defaultMockExt(), socketsExample)
}

// supply mock external interface, populates config from given file path
func mockConfigFromFile(t *testing.T, e External, path string) *Configuration {
	t.Helper()
	c := newDefaultConfiguration(e).
		WithProviderValidator(netdetect.ValidateProviderStub).
		WithNUMAValidator(netdetect.ValidateNUMAStub)
	c.Path = path

	if err := c.Load(); err != nil {
		t.Fatalf("failed to load %s: %s", path, err)
	}

	return c
}

func TestConfigMarshalUnmarshal(t *testing.T) {
	for name, tt := range map[string]struct {
		inExt  External
		inPath string
		errMsg string
	}{
		"uncommented default config": {
			defaultMockExt(),
			"uncommentedDefault",
			"",
		},
		"socket example config": {
			defaultMockExt(),
			socketsExample,
			"",
		},
		"psm2 example config": {
			defaultMockExt(),
			psm2Example,
			"",
		},
		"default empty config": {
			defaultMockExt(),
			defaultConfig,
			msgBadConfig + relConfExamplesPath + ": " + msgConfigNoProvider,
		},
		"nonexistent config": {
			defaultMockExt(),
			"/foo/bar/baz.yml",
			"reading file: open /foo/bar/baz.yml: no such file or directory",
		},
	} {
		t.Run(name, func(t *testing.T) {
			testDir, err := ioutil.TempDir("", strings.Replace(t.Name(), "/", "-", -1))
			defer os.RemoveAll(testDir)
			if err != nil {
				t.Fatal(err)
			}
			testFile := filepath.Join(testDir, "test.yml")

			if tt.inPath == "uncommentedDefault" {
				tt.inPath = filepath.Join(testDir, sConfigUncomment)
				uncommentServerConfig(t, tt.inPath)
			}

			configA := newDefaultConfiguration(tt.inExt).
				WithProviderValidator(netdetect.ValidateProviderStub).
				WithNUMAValidator(netdetect.ValidateNUMAStub)
			configA.Path = tt.inPath
			err = configA.Load()
			if err == nil {
				err = configA.Validate()
			}

			if tt.errMsg != "" {
				ExpectError(t, err, tt.errMsg, name)
				return
			}

			if err := configA.SaveToFile(testFile); err != nil {
				t.Fatal(err)
			}

			configB := newDefaultConfiguration(tt.inExt).
				WithProviderValidator(netdetect.ValidateProviderStub).
				WithNUMAValidator(netdetect.ValidateNUMAStub)
			if err := configB.SetPath(testFile); err != nil {
				t.Fatal(err)
			}

			err = configB.Load()
			if err == nil {
				err = configB.Validate()
			}

			if err != nil {
				t.Fatal(err)
			}

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreUnexported(
					Configuration{},
					security.CertificateConfig{},
				),
			}
			if diff := cmp.Diff(configA, configB, cmpOpts...); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}

func TestConstructedConfig(t *testing.T) {
	testDir, err := ioutil.TempDir("", strings.Replace(t.Name(), "/", "-", -1))
	defer os.RemoveAll(testDir)
	if err != nil {
		t.Fatal(err)
	}

	// First, load a config based on the server config with all options uncommented.
	testFile := filepath.Join(testDir, sConfigUncomment)
	uncommentServerConfig(t, testFile)
	defaultCfg := mockConfigFromFile(t, defaultMockExt(), testFile)

	var numaNode0 uint = 0
	var numaNode1 uint = 1

	// Next, construct a config to compare against the first one. It should be
	// possible to construct an identical configuration with the helpers.
	constructed := NewConfiguration().
		WithControlPort(10001).
		WithBdevInclude("0000:81:00.1", "0000:81:00.2", "0000:81:00.3").
		WithBdevExclude("0000:81:00.1").
		WithNrHugePages(4096).
		WithControlLogMask(ControlLogLevelError).
		WithControlLogFile("/tmp/daos_control.log").
		WithUserName("daosuser").
		WithGroupName("daosgroup").
		WithSystemName("daos").
		WithSocketDir("./.daos/daos_server").
		WithFabricProvider("ofi+verbs;ofi_rxm").
		WithAccessPoints("hostname1:10001").
		WithFaultCb("./.daos/fd_callback").
		WithFaultPath("/vcdu0/rack1/hostname").
		WithHyperthreads(true).
		WithProviderValidator(netdetect.ValidateProviderStub).
		WithNUMAValidator(netdetect.ValidateNUMAStub).
		WithServers(
			ioserver.NewConfig().
				WithRank(0).
				WithTargetCount(20).
				WithHelperStreamCount(0).
				WithServiceThreadCore(1).
				WithScmMountPoint("/mnt/daos/1").
				WithScmClass("ram").
				WithScmRamdiskSize(16).
				WithBdevClass("nvme").
				WithBdevDeviceList("0000:81:00.0").
				WithFabricInterface("qib0").
				WithFabricInterfacePort(20000).
				WithPinnedNumaNode(&numaNode0).
				WithEnvVars("CRT_TIMEOUT=30").
				WithLogFile("/tmp/daos_server1.log").
				WithLogMask("WARN"),
			ioserver.NewConfig().
				WithRank(1).
				WithTargetCount(20).
				WithHelperStreamCount(1).
				WithServiceThreadCore(22).
				WithScmMountPoint("/mnt/daos/2").
				WithScmClass("dcpm").
				WithScmDeviceList("/dev/pmem0").
				WithBdevClass("kdev").
				WithBdevDeviceList("/dev/sdc", "/dev/sdd").
				WithBdevDeviceCount(1).
				WithBdevFileSize(16).
				WithFabricInterface("qib0").
				WithFabricInterfacePort(20000).
				WithPinnedNumaNode(&numaNode1).
				WithEnvVars("CRT_TIMEOUT=100").
				WithLogFile("/tmp/daos_server2.log").
				WithLogMask("WARN"),
		)
	constructed.Path = testFile // just to avoid failing the cmp

	cmpOpts := []cmp.Option{
		cmpopts.IgnoreUnexported(
			Configuration{},
			security.CertificateConfig{},
		),
	}
	if diff := cmp.Diff(defaultCfg, constructed, cmpOpts...); diff != "" {
		t.Fatalf("(-want, +got): %s", diff)
	}
}

func TestConfigValidation(t *testing.T) {
	noopExtra := func(c *Configuration) *Configuration { return c }

	for name, tt := range map[string]struct {
		extraConfig func(c *Configuration) *Configuration
		errMsg      string
	}{
		"example config": {
			noopExtra,
			"",
		},
		"single access point": {
			func(c *Configuration) *Configuration {
				return c.WithAccessPoints("1.2.3.4:1234")
			},
			"",
		},
		"multiple access points": {
			func(c *Configuration) *Configuration {
				return c.WithAccessPoints("1.2.3.4:1234", "5.6.7.8:5678")
			},
			msgBadConfig + relConfExamplesPath + ": " + msgConfigBadAccessPoints,
		},
		"no access points": {
			func(c *Configuration) *Configuration {
				return c.WithAccessPoints()
			},
			msgBadConfig + relConfExamplesPath + ": " + msgConfigBadAccessPoints,
		},
		"single access point no port": {
			func(c *Configuration) *Configuration {
				return c.WithAccessPoints("1.2.3.4")
			},
			"",
		},
		"single access point invalid port": {
			func(c *Configuration) *Configuration {
				return c.WithAccessPoints("1.2.3.4").
					WithControlPort(0)
			},
			msgBadConfig + relConfExamplesPath + ": " + msgConfigBadPort,
		},
	} {
		t.Run(name, func(t *testing.T) {
			testDir, err := ioutil.TempDir("", strings.Replace(t.Name(), "/", "-", -1))
			defer os.RemoveAll(testDir)
			if err != nil {
				t.Fatal(err)
			}

			// First, load a config based on the server config with all options uncommented.
			testFile := filepath.Join(testDir, sConfigUncomment)
			uncommentServerConfig(t, testFile)
			config := mockConfigFromFile(t, defaultMockExt(), testFile)

			// Apply extra config test case
			config = tt.extraConfig(config)

			err = config.Validate()
			if tt.errMsg != "" {
				ExpectError(t, err, tt.errMsg, name)
				return
			}

			if err != nil {
				t.Fatal(err)
			}
		})
	}
}
