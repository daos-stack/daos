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

package main

import (
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	. "github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/log"
	"github.com/pkg/errors"
)

const (
	sConfig = "../../../utils/config/daos_server.yml"
	// sConfigUncomment created on init
	sConfigUncomment = "testdata/.daos_server_uncomment.yml"
	socketsExample   = "../../../utils/config/examples/daos_server_sockets.yml"
	psm2Example      = "../../../utils/config/examples/daos_server_psm2.yml"
	defaultConfig    = "../../../utils/config/daos_server.yml"
	tmpIn            = "testdata/.tmp_in.yml"
	tmpOut           = "testdata/.tmp_out.yml"
)

func init() {
	log.NewDefaultLogger(log.Error, "config_test: ", os.Stderr)

	// load uncommented version of canonical config file daos_server.yml
	uncommentServerConfig()
}

// uncommentServerConfig removes leading comment chars from daos_server.yml
// lines in order to verify parsing of all available params.
func uncommentServerConfig() {
	fail := func(e error) {
		log.Errorf(e.Error())
		os.Exit(1)
	}

	cmd := exec.Command(
		"bash", "-c", fmt.Sprintf("sed s/^#//g %s > %s", sConfig, sConfigUncomment))

	stderr, err := cmd.StderrPipe()
	if err != nil {
		fail(err)
	}

	if err := cmd.Start(); err != nil {
		fail(err)
	}

	slurp, _ := ioutil.ReadAll(stderr)
	if string(slurp) != "" {
		fail(errors.New(string(slurp)))
	}

	if err := cmd.Wait(); err != nil {
		fail(err)
	}
}

// newMockConfig returns default configuration with mocked external interface
func newMockConfig(
	cmdRet error, getenvRet string, existsRet bool, mountRet error,
	unmountRet error, mkdirRet error, removeRet error) configuration {

	return newDefaultConfiguration(
		&mockExt{
			cmdRet, getenvRet, existsRet, mountRet, unmountRet,
			mkdirRet, removeRet})
}

// defaultMockConfig returns MockConfig with default mock return values
func defaultMockConfig() configuration {
	return newMockConfig(nil, "", false, nil, nil, nil, nil)
}
func cmdFailsConfig() configuration {
	return newMockConfig(errors.New("exit status 1"), "", false, nil, nil, nil, nil)
}
func envExistsConfig() configuration {
	return newMockConfig(nil, "somevalue", false, nil, nil, nil, nil)
}

func populateMockConfig(t *testing.T, c configuration, path string) configuration {
	c.Path = path
	err := c.loadConfig()
	if err != nil {
		t.Fatalf("Configuration could not be read (%s)", err)
	}
	return c
}

// TestParseConfigSucceed verifies expected input yaml configs match expected output
// yaml after being encoded and decoded. Input and expected output combinations read
// from 2 files with multiple entries.
// Write input to file, loadConfig (decode), saveConf (encode) and compare written yaml.
func TestParseConfigSucceed(t *testing.T) {
	inputYamls, outputYamls, err := LoadTestFiles(
		"testdata/input_good.txt", "testdata/output_success.txt")
	if err != nil {
		t.Fatal(err)
	}

	for i, y := range inputYamls {
		// write input yaml config to temporary file
		err := WriteSlice(tmpIn, y)
		if err != nil {
			t.Fatal(err)
		}
		// verify decoding of config from written file
		config := defaultMockConfig()
		config.Path = tmpIn
		err = config.loadConfig()
		if err != nil {
			t.Fatal(err)
		}
		// encode decoded config to temporary output file
		err = config.saveConfig(tmpOut)
		if err != nil {
			t.Fatal(err)
		}
		// use SplitFile (just for convenience) to read output file contents
		outputs, err := SplitFile(tmpOut)
		if err != nil {
			t.Fatal(err)
		}
		// compare encoded output (first element of output from SplitFile)
		// with expected outputYaml
		for x, line := range outputYamls[i] {
			AssertEqual(
				t, outputs[0][x], line,
				fmt.Sprintf(
					"line %d parsed input %s doesn't match expected output:\n\thave %#v\n\twant %#v\n from input %#v\n",
					x, outputs[0], outputs[0][x], line, y))
		}
	}
}

// TestParseConfigFail verifies bad input yaml configs result in failure with expected
// error messages. Input and expected error message text combinations read
// from 2 files with multiple entries.
// Write input to file, loadConfig (decode) should fail, compare error message.
func TestParseConfigFail(t *testing.T) {
	inputYamls, outputErrorMsgs, err := LoadTestFiles(
		"testdata/input_bad.txt", "testdata/output_errors.txt")
	if err != nil {
		t.Fatal(err)
	}

	for i, y := range inputYamls {
		err := WriteSlice(tmpIn, y)
		if err != nil {
			t.Fatal(err)
		}

		config := defaultMockConfig()
		config.Path = tmpIn
		err = config.loadConfig()
		// output error messages will always be first entry in a slice.
		ExpectError(t, err, outputErrorMsgs[i][0], "")
	}
}

// TestProvidedConfigs verifies that the provided server config matches what we expect
// after being decoded.
func TestProvidedConfigs(t *testing.T) {
	tests := []struct {
		inConfig configuration
		inPath   string
		desc     string
		errMsg   string
	}{
		{
			defaultMockConfig(),
			sConfigUncomment,
			"uncommented default config",
			"",
		},
		{
			defaultMockConfig(),
			socketsExample,
			"socket example config",
			"",
		},
		{
			defaultMockConfig(),
			psm2Example,
			"psm2 example config",
			"",
		},
		{
			defaultMockConfig(),
			defaultConfig,
			"default empty config",
			"required parameters missing from config and os environment (CRT_PHY_ADDR_STR)",
		},
		{
			envExistsConfig(),
			defaultConfig,
			"default empty config with os env present",
			"",
		},
	}

	for _, tt := range tests {
		// compute file path containing expected output to compare with
		// some input files may be auto generated and prefixed with "."
		// the relevant expect files will not be so strip here
		filename := strings.TrimPrefix(filepath.Base(tt.inPath), ".")
		expectedFile := fmt.Sprintf("testdata/expect_%s", filename)
		config := populateMockConfig(t, tt.inConfig, tt.inPath)
		// encode decoded config to temporary output file to verify parser
		err := config.saveConfig(tmpOut)
		if err != nil {
			t.Fatal(err)
		}
		// use SplitFile (just for convenience) to read what has been output
		outYamls, err := SplitFile(tmpOut)
		if err != nil {
			t.Fatal(
				errors.Wrapf(
					err,
					"reading processed output file %s",
					tmpOut))
		}
		// read and compare output file contents with expected file contents
		// (outFile), (extract first element of output from SplitFile for each)
		outExpectYamls, err := SplitFile(expectedFile)
		if err != nil {
			t.Fatal(
				errors.Wrapf(
					err,
					"reading expected output file %s",
					expectedFile))
		}

		// verify the generated yaml is of expected size
		outExpectYaml := outExpectYamls[0]
		outYaml := outYamls[0]

		// verify the generated yaml is of expected content
		for i, line := range outExpectYaml {
			AssertEqual(
				t, outYaml[i], line,
				fmt.Sprintf(
					"line %d parsed %s config doesn't match fixture %s:\n\thave %#v\n\twant %#v\n",
					i, tt.inPath, expectedFile, outYaml[i], line))
		}

		if len(outExpectYaml) != len(outYaml) {
			t.Fatalf("number of lines unexpected in %s", expectedFile)
		}

		// retrieve expected and actual io parameters
		expectedFile = fmt.Sprintf(
			"testdata/ioparams_%s.txt",
			strings.TrimSuffix(filename, filepath.Ext(tt.inPath)))
		outExpect, err := SplitFile(expectedFile)
		if err != nil {
			t.Fatal(
				errors.Wrapf(
					err,
					"reading expected output file %s",
					expectedFile))
		}
		err = config.getIOParams(&cliOptions{})
		if tt.errMsg != "" {
			ExpectError(t, err, tt.errMsg, tt.desc)
			continue
		}
		if err != nil {
			t.Fatal(
				errors.Wrapf(
					err,
					"retrieving IO params using conf %s",
					filename))
		}
		// should only ever be one line in expected output, compare
		// string representations of config.Servers
		actual := strings.Split(fmt.Sprintf("%+v", config.Servers), " ")
		exp := strings.Split(outExpect[0][0], " ")

		for i, s := range actual {
			AssertEqual(
				t, s, exp[i],
				fmt.Sprintf("parameters don't match %s", expectedFile))
		}

		// in case extra values were expected
		AssertEqual(
			t, len(actual), len(exp),
			fmt.Sprintf("size of parameters don't match %s", expectedFile))
	}
}

func TestGetNumCores(t *testing.T) {
	tests := []struct {
		cpus   []string
		cores  int
		errMsg string
	}{
		{nil, 0, ""},
		{[]string{}, 0, ""},
		{[]string{"1-8"}, 8, ""},
		{[]string{"0-7", "20-26"}, 15, ""},
		{[]string{"0-1"}, 2, ""},
		{[]string{"0"}, 1, ""},
		{[]string{"1", "5"}, 2, ""},
		{[]string{"0-i"}, 15, "strconv.Atoi: parsing \"i\": invalid syntax"},
		{[]string{"blah"}, 15, "strconv.Atoi: parsing \"blah\": invalid syntax"},
		{[]string{"0-8-8"}, 8, "unsupported range format 0-8-8, need <int>-<int> e.g. 1-10"},
		{[]string{"8-8"}, 8, "unsupported range format 8-8, need <int>-<int> e.g. 1-10"},
		{[]string{"8-1"}, 8, "unsupported range format 8-1, need <int>-<int> e.g. 1-10"},
		{[]string{"0-0"}, 0, "unsupported range format 0-0, need <int>-<int> e.g. 1-10"},
	}

	for _, tt := range tests {
		num, err := getNumCores(tt.cpus)
		if tt.errMsg != "" {
			ExpectError(t, err, tt.errMsg, tt.cpus)
			continue
		}
		if err != nil {
			t.Fatal(err)
		}
		AssertEqual(t, num, tt.cores, "unexpected number of cores calculated")
	}
}

func TestSetNumCores(t *testing.T) {
	tests := []struct {
		num    int
		cpus   []string
		errMsg string
	}{
		{8, []string{"0-7"}, ""},
		{10, []string{"0-9"}, ""},
		{1, []string{"0"}, ""},
		{2, []string{"0-1"}, ""},
		{0, []string{"0-0"}, "invalid number of cpus (cores) specified: 0"},
	}

	for _, tt := range tests {
		cpus, err := setNumCores(tt.num)
		if tt.errMsg != "" {
			ExpectError(t, err, tt.errMsg, tt.num)
			continue
		}
		AssertEqual(t, cpus, tt.cpus, "failed to convert number to range")
		num, err := getNumCores(cpus)
		if err != nil {
			t.Fatal(err)
		}
		AssertEqual(t, num, tt.num, "failed to convert to expected number")
	}
}

// TestCmdlineOverride verified that cliOpts take precedence over existing
// configs resulting in overrides appearing in ioparams
func TestCmdlineOverride(t *testing.T) {
	r := rank(9)
	m := "moduleA moduleB"
	a := "/some/file"
	y := "/another/different/file"

	// test-local function to generate configuration
	// (mock with default behaviours populated with uncommented daos_server.yml)
	newC := func(t *testing.T) configuration {
		return populateMockConfig(t, defaultMockConfig(), sConfigUncomment)
	}

	tests := []struct {
		inCliOpts  cliOptions
		inConfig   configuration
		outCliOpts [][]string
		desc       string
		errMsg     string
	}{
		{
			inConfig: newC(t),
			outCliOpts: [][]string{
				{
					"-t", "21",
					"-g", "daos",
					"-s", "/mnt/daos/1",
					"-x", "0",
					"-r", "0",
					"-d", "./.daos/daos_server",
				},
				{
					"-t", "20",
					"-g", "daos",
					"-s", "/mnt/daos/2",
					"-x", "0",
					"-r", "1",
					"-d", "./.daos/daos_server",
				},
			},
			desc: "None",
		},
		{
			inCliOpts: cliOptions{MountPath: "/foo/bar"},
			inConfig:  newC(t),
			outCliOpts: [][]string{
				{
					"-t", "21",
					"-g", "daos",
					"-s", "/foo/bar",
					"-x", "0",
					"-r", "0",
					"-d", "./.daos/daos_server",
				},
				{
					"-t", "20",
					"-g", "daos",
					"-s", "/foo/bar",
					"-x", "0",
					"-r", "1",
					"-d", "./.daos/daos_server",
				},
			},
			desc: "MountPath",
		},
		{
			inCliOpts: cliOptions{Group: "testing123"},
			inConfig:  newC(t),
			outCliOpts: [][]string{
				{
					"-t", "21",
					"-g", "testing123",
					"-s", "/mnt/daos/1",
					"-x", "0",
					"-r", "0",
					"-d", "./.daos/daos_server",
				},
				{
					"-t", "20",
					"-g", "testing123",
					"-s", "/mnt/daos/2",
					"-x", "0",
					"-r", "1",
					"-d", "./.daos/daos_server",
				},
			},
			desc: "Group",
		},
		{
			inCliOpts: cliOptions{Cores: 2},
			inConfig:  newC(t),
			outCliOpts: [][]string{
				{
					"-t", "2",
					"-g", "daos",
					"-s", "/mnt/daos/1",
					"-x", "0",
					"-r", "0",
					"-d", "./.daos/daos_server",
				},
				{
					"-t", "2",
					"-g", "daos",
					"-s", "/mnt/daos/2",
					"-x", "0",
					"-r", "1",
					"-d", "./.daos/daos_server",
				},
			},
			desc: "Cores",
		},
		{
			inCliOpts: cliOptions{Rank: &r},
			inConfig:  newC(t),
			outCliOpts: [][]string{
				{
					"-t", "21",
					"-g", "daos",
					"-s", "/mnt/daos/1",
					"-x", "0",
					"-r", "9",
					"-d", "./.daos/daos_server",
				},
				{
					"-t", "20",
					"-g", "daos",
					"-s", "/mnt/daos/2",
					"-x", "0",
					"-r", "1",
					"-d", "./.daos/daos_server",
				},
			},
			desc: "Rank",
		},
		{
			// currently not provided as config or cli option, set
			// directly in configuration
			inConfig: func() configuration {
				c := defaultMockConfig()
				c.NvmeShmID = 1
				return populateMockConfig(t, c, sConfigUncomment)
			}(),
			outCliOpts: [][]string{
				{
					"-t", "21",
					"-g", "daos",
					"-s", "/mnt/daos/1",
					"-x", "0",
					"-r", "0",
					"-d", "./.daos/daos_server",
					"-i", "1",
				},
				{
					"-t", "20",
					"-g", "daos",
					"-s", "/mnt/daos/2",
					"-x", "0",
					"-r", "1",
					"-d", "./.daos/daos_server",
					"-i", "1",
				},
			},
			desc: "NvmeShmID",
		},
		{
			inCliOpts: cliOptions{SocketDir: "/tmp/Jeremy", Modules: &m, Attach: &a, Map: &y},
			inConfig:  newC(t),
			outCliOpts: [][]string{
				{
					"-t", "21",
					"-g", "daos",
					"-s", "/mnt/daos/1",
					"-m", "moduleA moduleB",
					"-a", "/some/file",
					"-x", "0",
					"-y", "/another/different/file",
					"-r", "0",
					"-d", "/tmp/Jeremy",
				},
				{
					"-t", "20",
					"-g", "daos",
					"-s", "/mnt/daos/2",
					"-m", "moduleA moduleB",
					"-a", "/some/file",
					"-x", "0",
					"-y", "/another/different/file",
					"-r", "1",
					"-d", "/tmp/Jeremy",
				},
			},
			desc: "SocketDir Modules Attach Map",
		},
		{
			inCliOpts: cliOptions{Cores: 2, Targets: 5},
			inConfig:  newC(t),
			outCliOpts: [][]string{
				{
					"-t", "5",
					"-g", "daos",
					"-s", "/mnt/daos/1",
					"-x", "0",
					"-r", "0",
					"-d", "./.daos/daos_server",
				},
				{
					"-t", "5",
					"-g", "daos",
					"-s", "/mnt/daos/2",
					"-x", "0",
					"-r", "1",
					"-d", "./.daos/daos_server",
				},
			},
			desc: "Targets cli overrides Cores cli",
		},
		{
			inCliOpts: cliOptions{Cores: 2},
			inConfig: func() configuration {
				c := defaultMockConfig()
				c.Targets = 3
				return populateMockConfig(t, c, sConfigUncomment)
			}(),
			outCliOpts: [][]string{
				{
					"-t", "3",
					"-g", "daos",
					"-s", "/mnt/daos/1",
					"-x", "0",
					"-r", "0",
					"-d", "./.daos/daos_server",
				},
				{
					"-t", "3",
					"-g", "daos",
					"-s", "/mnt/daos/2",
					"-x", "0",
					"-r", "1",
					"-d", "./.daos/daos_server",
				},
			},
			desc: "Targets config overrides Cores cli",
		},
		{
			// no provider set but os env set mock getenv returns not empty string
			inConfig: envExistsConfig(),
			outCliOpts: [][]string{
				{
					"-t", "0",
					"-g", "daos_server",
					"-s", "/mnt/daos",
					"-x", "0",
					"-d", "/var/run/daos_server",
				},
			},
			desc: "use defaults, no Provider set but provider env exists",
		},
		{
			// no provider set but os env set mock getenv returns not empty string
			inCliOpts: cliOptions{
				Cores: 2, Group: "bob", MountPath: "/foo/bar",
				SocketDir: "/tmp/Jeremy", Modules: &m, Attach: &a, Map: &y},
			inConfig: envExistsConfig(),
			outCliOpts: [][]string{
				{
					"-t", "2",
					"-g", "bob",
					"-s", "/foo/bar",
					"-m", "moduleA moduleB",
					"-a", "/some/file",
					"-x", "0",
					"-y", "/another/different/file",
					"-d", "/tmp/Jeremy",
				},
			},
			desc: "override defaults, no Provider set but provider env exists",
		},
		{
			// no provider set and no os env set mock getenv returns empty string
			inCliOpts: cliOptions{
				Cores: 2, Group: "bob", MountPath: "/foo/bar",
				SocketDir: "/tmp/Jeremy", Modules: &m, Attach: &a, Map: &y},
			inConfig: defaultMockConfig(),
			desc:     "override defaults, no Provider set and no provider env exists",
			errMsg:   "required parameters missing from config and os environment (CRT_PHY_ADDR_STR)",
		},
	}

	for _, tt := range tests {
		config := tt.inConfig
		err := config.getIOParams(&tt.inCliOpts)
		if tt.errMsg != "" {
			ExpectError(t, err, tt.errMsg, tt.desc)
			continue
		}
		if err != nil {
			t.Fatalf("Params could not be generated (%s: %s)", tt.desc, err)
		}
		if len(config.Servers) != len(tt.outCliOpts) {
			t.Fatalf(
				"incorrect number of io server params returned (%s)", tt.desc)
		}

		for i, expOpts := range tt.outCliOpts {
			AssertEqual(
				t, config.Servers[i].CliOpts, expOpts,
				fmt.Sprintf(
					"cli options for io_server %d unexpected (changed: %s)",
					i, tt.desc))
		}
	}
}

func TestPopulateEnv(t *testing.T) {
	tests := []struct {
		inConfig  configuration
		ioIdx     int
		inEnvs    []string
		outEnvs   []string
		getParams bool
		desc      string
	}{
		{
			defaultMockConfig(),
			0,
			[]string{},
			[]string{},
			false,
			"empty config (no envs) and getenv returns empty",
		},
		{
			defaultMockConfig(),
			0,
			[]string{"FOO=bar"},
			[]string{"FOO=bar"},
			false,
			"empty config (no envs) and getenv returns empty",
		},
		{
			populateMockConfig(t, defaultMockConfig(), socketsExample),
			0,
			[]string{"FOO=bar"},
			[]string{
				"FOO=bar",
				"ABT_ENV_MAX_NUM_XSTREAMS=100",
				"ABT_MAX_NUM_XSTREAMS=100",
				"DAOS_MD_CAP=1024",
				"CRT_CTX_SHARE_ADDR=0",
				"CRT_TIMEOUT=30",
				"FI_SOCKETS_MAX_CONN_RETRY=1",
				"FI_SOCKETS_CONN_TIMEOUT=2000",
				"CRT_PHY_ADDR_STR=ofi+sockets",
				"OFI_INTERFACE=eth0",
				"OFI_PORT=31416",
				"D_LOG_MASK=ERR",
				"D_LOG_FILE=/tmp/server.log",
			},
			true,
			"sockets populated config (with envs) and getenv returns empty",
		},
		{
			populateMockConfig(t, envExistsConfig(), socketsExample),
			0,
			// existing os vars already set, config values should be ignored and
			// result in no change
			[]string{
				"FOO=bar",
				"ABT_MAX_NUM_XSTREAMS=somevalue",
				"DAOS_MD_CAP=somevalue",
				"CRT_TIMEOUT=somevalue",
				"FI_SOCKETS_MAX_CONN_RETRY=somevalue",
				"FI_SOCKETS_CONN_TIMEOUT=somevalue",
				"CRT_PHY_ADDR_STR=somevalue",
				"OFI_INTERFACE=somevalue",
				"OFI_PORT=somevalue",
				"D_LOG_MASK=somevalue",
				"D_LOG_FILE=somevalue",
			},
			[]string{
				"FOO=bar",
				"ABT_MAX_NUM_XSTREAMS=somevalue",
				"DAOS_MD_CAP=somevalue",
				"CRT_TIMEOUT=somevalue",
				"FI_SOCKETS_MAX_CONN_RETRY=somevalue",
				"FI_SOCKETS_CONN_TIMEOUT=somevalue",
				"CRT_PHY_ADDR_STR=somevalue",
				"OFI_INTERFACE=somevalue",
				"OFI_PORT=somevalue",
				"D_LOG_MASK=somevalue",
				"D_LOG_FILE=somevalue",
			},
			true,
			"sockets populated config (with envs) and getenv returns with 'somevalue'",
		},
		{
			populateMockConfig(t, defaultMockConfig(), psm2Example),
			0,
			[]string{"FOO=bar"},
			[]string{
				"FOO=bar",
				"ABT_ENV_MAX_NUM_XSTREAMS=100",
				"ABT_MAX_NUM_XSTREAMS=100",
				"DAOS_MD_CAP=1024",
				"CRT_CTX_SHARE_ADDR=0",
				"CRT_TIMEOUT=30",
				"CRT_CREDIT_EP_CTX=0",
				"CRT_PHY_ADDR_STR=ofi+psm2",
				"OFI_INTERFACE=ib0",
				"OFI_PORT=31416",
				"D_LOG_MASK=ERR",
				"D_LOG_FILE=/tmp/server.log",
			},
			true,
			"psm2 populated config (with envs) and getenv returns empty",
		},
		{
			populateMockConfig(t, envExistsConfig(), psm2Example),
			0,
			// existing os vars already set, config values should be ignored and
			// result in no change, as provider is set in os, no changes made
			[]string{
				"FOO=bar",
				"ABT_MAX_NUM_XSTREAMS=somevalue",
				"DAOS_MD_CAP=somevalue",
				"CRT_TIMEOUT=somevalue",
				"FI_SOCKETS_MAX_CONN_RETRY=somevalue",
				"FI_SOCKETS_CONN_TIMEOUT=somevalue",
				"CRT_PHY_ADDR_STR=somevalue",
				"OFI_INTERFACE=somevalue",
				"OFI_PORT=somevalue",
				"D_LOG_MASK=somevalue",
				"D_LOG_FILE=somevalue",
			},
			[]string{
				"FOO=bar",
				"ABT_MAX_NUM_XSTREAMS=somevalue",
				"DAOS_MD_CAP=somevalue",
				"CRT_TIMEOUT=somevalue",
				// "CRT_CREDIT_EP_CTX=0", // whilst this is a new env, it is ignored
				"FI_SOCKETS_MAX_CONN_RETRY=somevalue",
				"FI_SOCKETS_CONN_TIMEOUT=somevalue",
				"CRT_PHY_ADDR_STR=somevalue",
				"OFI_INTERFACE=somevalue",
				"OFI_PORT=somevalue",
				"D_LOG_MASK=somevalue",
				"D_LOG_FILE=somevalue",
			},
			true,
			"psm2 populated config (with envs) and getenv returns with 'somevalue'",
		},
	}

	for _, tt := range tests {
		config := tt.inConfig
		inEnvs := tt.inEnvs
		if len(config.Servers) == 0 {
			server := newDefaultServer()
			config.Servers = append(config.Servers, server)
		}
		// optionally add to server EnvVars from config (with empty cliOptions)
		if tt.getParams == true {
			err := config.getIOParams(&cliOptions{})
			if err != nil {
				t.Fatalf("Params could not be generated (%s: %s)", tt.desc, err)
			}
		}
		// pass in env and verify output envs is as expected
		config.populateEnv(tt.ioIdx, &inEnvs)
		AssertEqual(t, inEnvs, tt.outEnvs, tt.desc)
	}
}
