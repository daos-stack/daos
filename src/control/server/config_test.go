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

var (
	sConfig = "../../../utils/config/daos_server.yml"
	// sConfigUncomment not written before tests start
	sConfigUncomment = "testdata/.daos_server_uncomment.yml"
	socketsExample   = "../../../utils/config/examples/daos_server_sockets.yml"
	psm2Example      = "../../../utils/config/examples/daos_server_psm2.yml"
	defaultConfig    = "../../../utils/config/daos_server.yml"
	tmpIn            = "testdata/.tmp_in.yml"
	tmpOut           = "testdata/.tmp_out.yml"
	testInit         = 0
	// files is a mock store of written file contents
	files    = []string{}
	commands = []string{}
)

func init() {
	log.NewDefaultLogger(log.Error, "config_test: ", os.Stderr)
}

func setupTest(t *testing.T) {
	files = []string{}
	commands = []string{}
}

func uncommentServerConfig(t *testing.T) {
	if testInit == 0 {
		// remove leading comment from daos_server.yml lines to verify setting all
		// non-default params.
		cmd := exec.Command(
			"bash", "-c", fmt.Sprintf("sed s/^#//g %s > %s", sConfig, sConfigUncomment))

		stderr, err := cmd.StderrPipe()
		if err != nil {
			t.Fatal(err)
		}

		if err := cmd.Start(); err != nil {
			t.Fatal(err)
		}

		slurp, _ := ioutil.ReadAll(stderr)
		if string(slurp) != "" {
			t.Error(slurp)
		}

		if err := cmd.Wait(); err != nil {
			t.Fatal(err)
		}
		testInit = 1
	}
}

type mockExt struct {
	// return error if cmd in shell fails
	cmdRet error
	// return empty string if os env not set/set empty
	getenvRet string
	// return true if file already exists
	existsRet bool
}

func (m *mockExt) runCommand(cmd string) error {
	commands = append(commands, cmd)
	return m.cmdRet
}
func (m *mockExt) getenv(key string) string { return m.getenvRet }
func (m *mockExt) writeToFile(in string, outPath string) error {
	files = append(files, fmt.Sprint(outPath, ":", in))
	return nil
}
func (m *mockExt) createEmpty(path string, size int64) error {
	if !m.existsRet {
		files = append(files, fmt.Sprint(path, ":empty size ", size))
	}
	return nil
}

// newMockConfig returns default configuration with mocked external interface
func newMockConfig(
	cmdRet error, getenvRet string, existsRet bool) configuration {
	return newDefaultConfiguration(
		&mockExt{cmdRet, getenvRet, existsRet})
}

// newDefaultMockConfig returns MockConfig with default mock return values
func newDefaultMockConfig() configuration {
	return newMockConfig(nil, "", false)
}
func cmdFailsConfig() configuration {
	return newMockConfig(errors.New("exit status 1"), "", false)
}
func envExistsConfig() configuration {
	return newMockConfig(nil, "somevalue", false)
}
func fileExistsConfig() configuration {
	return newMockConfig(nil, "", true)
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
		config := newDefaultMockConfig()
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

		config := newDefaultMockConfig()
		config.Path = tmpIn
		err = config.loadConfig()
		// output error messages will always be first entry in a slice.
		ExpectError(t, err, outputErrorMsgs[i][0], "")
	}
}

// TestProvidedConfigs verifies that the provided server config matches what we expect
// after being decoded.
func TestProvidedConfigs(t *testing.T) {
	uncommentServerConfig(t)

	tests := []struct {
		inConfig configuration
		inPath   string
		desc     string
		errMsg   string
	}{
		{
			newDefaultMockConfig(),
			sConfigUncomment,
			"uncommented default config",
			"",
		},
		{
			newDefaultMockConfig(),
			socketsExample,
			"socket example config",
			"",
		},
		{
			newDefaultMockConfig(),
			psm2Example,
			"psm2 example config",
			"",
		},
		{
			newDefaultMockConfig(),
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
		if len(outExpectYaml) != len(outYaml) {
			t.Fatalf("number of lines unexpected in %s", expectedFile)
		}

		// verify the generated yaml is of expected content
		for i, line := range outExpectYaml {
			AssertEqual(
				t, outYaml[i], line,
				fmt.Sprintf(
					"line %d parsed %s config doesn't match fixture %s:\n\thave %#v\n\twant %#v\n",
					i, tt.inPath, expectedFile, outYaml[i], line))
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
		AssertEqual(
			t, fmt.Sprintf("%+v", config.Servers), outExpect[0][0],
			fmt.Sprintf("parameters don't match %s", expectedFile))
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

	uncommentServerConfig(t)

	// test-local function to generate configuration
	// (mock with default behaviours populated with uncommented daos_server.yml)
	newC := func(t *testing.T) configuration {
		return populateMockConfig(t, newDefaultMockConfig(), sConfigUncomment)
	}

	tests := []struct {
		inCliOpts  cliOptions
		inConfig   configuration
		outCliOpts [][]string
		expNumCmds int // number of commands expected to have been run as side effect
		desc       string
		errMsg     string
		expCmds    []string // list of expected commands to have been run
	}{
		{
			inConfig: newC(t),
			outCliOpts: [][]string{
				{
					"-c", "21",
					"-g", "daos",
					"-s", "/mnt/daos/1",
					"-r", "0",
					"-d", "./.daos/daos_server",
				},
				{
					"-c", "20",
					"-g", "daos",
					"-s", "/mnt/daos/2",
					"-r", "1",
					"-d", "./.daos/daos_server",
				},
			},
			expNumCmds: 2,
			desc:       "None",
		},
		{
			inConfig:   populateMockConfig(t, cmdFailsConfig(), sConfigUncomment),
			expNumCmds: 2,
			desc:       "None, mount check fails",
			errMsg:     "server0 scm mount path (/mnt/daos/1) not mounted: exit status 1",
			expCmds: []string{
				"mount | grep ' /mnt/daos/1 '",
				"mount | grep ' /mnt/daos '",
			},
		},
		{
			inCliOpts: cliOptions{MountPath: "/foo/bar"},
			inConfig:  newC(t),
			outCliOpts: [][]string{
				{
					"-c", "21",
					"-g", "daos",
					"-s", "/foo/bar",
					"-r", "0",
					"-d", "./.daos/daos_server",
				},
				{
					"-c", "20",
					"-g", "daos",
					"-s", "/foo/bar",
					"-r", "1",
					"-d", "./.daos/daos_server",
				},
			},
			expNumCmds: 2,
			desc:       "MountPath",
		},
		{
			inCliOpts: cliOptions{Group: "testing123"},
			inConfig:  newC(t),
			outCliOpts: [][]string{
				{
					"-c", "21",
					"-g", "testing123",
					"-s", "/mnt/daos/1",
					"-r", "0",
					"-d", "./.daos/daos_server",
				},
				{
					"-c", "20",
					"-g", "testing123",
					"-s", "/mnt/daos/2",
					"-r", "1",
					"-d", "./.daos/daos_server",
				},
			},
			expNumCmds: 2,
			desc:       "Group",
		},
		{
			inCliOpts: cliOptions{Cores: 2},
			inConfig:  newC(t),
			outCliOpts: [][]string{
				{
					"-c", "2",
					"-g", "daos",
					"-s", "/mnt/daos/1",
					"-r", "0",
					"-d", "./.daos/daos_server",
				},
				{
					"-c", "2",
					"-g", "daos",
					"-s", "/mnt/daos/2",
					"-r", "1",
					"-d", "./.daos/daos_server",
				},
			},
			expNumCmds: 2,
			desc:       "Cores",
		},
		{
			inCliOpts: cliOptions{Rank: &r},
			inConfig:  newC(t),
			outCliOpts: [][]string{
				{
					"-c", "21",
					"-g", "daos",
					"-s", "/mnt/daos/1",
					"-r", "9",
					"-d", "./.daos/daos_server",
				},
				{
					"-c", "20",
					"-g", "daos",
					"-s", "/mnt/daos/2",
					"-r", "1",
					"-d", "./.daos/daos_server",
				},
			},
			expNumCmds: 2,
			desc:       "Rank",
		},
		{
			// currently not provided as config or cli option, set
			// directly in configuration
			inConfig: func() configuration {
				c := newDefaultMockConfig()
				c.NvmeShmID = 1
				return populateMockConfig(t, c, sConfigUncomment)
			}(),
			outCliOpts: [][]string{
				{
					"-c", "21",
					"-g", "daos",
					"-s", "/mnt/daos/1",
					"-r", "0",
					"-d", "./.daos/daos_server",
					"-i", "1",
				},
				{
					"-c", "20",
					"-g", "daos",
					"-s", "/mnt/daos/2",
					"-r", "1",
					"-d", "./.daos/daos_server",
					"-i", "1",
				},
			},
			expNumCmds: 2,
			desc:       "NvmeShmID",
		},
		{
			inCliOpts: cliOptions{SocketDir: "/tmp/Jeremy", Modules: &m, Attach: &a, Map: &y},
			inConfig:  newC(t),
			outCliOpts: [][]string{
				{
					"-c", "21",
					"-g", "daos",
					"-s", "/mnt/daos/1",
					"-m", "moduleA moduleB",
					"-a", "/some/file",
					"-y", "/another/different/file",
					"-r", "0",
					"-d", "/tmp/Jeremy",
				},
				{
					"-c", "20",
					"-g", "daos",
					"-s", "/mnt/daos/2",
					"-m", "moduleA moduleB",
					"-a", "/some/file",
					"-y", "/another/different/file",
					"-r", "1",
					"-d", "/tmp/Jeremy",
				},
			},
			expNumCmds: 2,
			desc:       "SocketDir Modules Attach Map",
		},
		{
			// no provider set but os env set mock getenv returns not empty string
			inConfig: envExistsConfig(),
			outCliOpts: [][]string{
				{
					"-c", "0",
					"-g", "daos_server",
					"-s", "/mnt/daos",
					"-d", "/var/run/daos_server",
				},
			},
			expNumCmds: 1,
			desc:       "use defaults, no Provider set but provider env exists",
		},
		{
			// no provider set but os env set mock getenv returns not empty string
			inCliOpts: cliOptions{
				Cores: 2, Group: "bob", MountPath: "/foo/bar",
				SocketDir: "/tmp/Jeremy", Modules: &m, Attach: &a, Map: &y},
			inConfig: envExistsConfig(),
			outCliOpts: [][]string{
				{
					"-c", "2",
					"-g", "bob",
					"-s", "/foo/bar",
					"-m", "moduleA moduleB",
					"-a", "/some/file",
					"-y", "/another/different/file",
					"-d", "/tmp/Jeremy",
				},
			},
			expNumCmds: 1,
			desc:       "override defaults, no Provider set but provider env exists",
		},
		{
			// no provider set and no os env set mock getenv returns empty string
			inCliOpts: cliOptions{
				Cores: 2, Group: "bob", MountPath: "/foo/bar",
				SocketDir: "/tmp/Jeremy", Modules: &m, Attach: &a, Map: &y},
			inConfig: newDefaultMockConfig(),
			desc:     "override defaults, no Provider set and no provider env exists",
			errMsg:   "required parameters missing from config and os environment (CRT_PHY_ADDR_STR)",
		},
		{
			// no provider set but os env set mock getenv returns not empty string
			// mount not set and returns invalid
			inConfig: newMockConfig(errors.New("exit status 1"), "somevalue", false),
			desc:     "use defaults, mount check fails with default",
			errMsg:   "server0 scm mount path (/mnt/daos) not mounted: exit status 1",
			expCmds: []string{
				"mount | grep ' /mnt/daos '",
				"mount | grep ' /mnt '",
			},
		},
		{
			// no provider set but os env set mock getenv returns not empty string
			// mount set and returns invalid
			inCliOpts: cliOptions{MountPath: "/foo/bar"},
			inConfig:  newMockConfig(errors.New("exit status 1"), "somevalue", false),
			desc:      "override MountPath, mount check fails with new value",
			errMsg:    "server0 scm mount path (/foo/bar) not mounted: exit status 1",
			expCmds: []string{
				"mount | grep ' /foo/bar '",
				"mount | grep ' /foo '",
			},
		},
	}

	for _, tt := range tests {
		setupTest(t)
		config := tt.inConfig
		err := config.getIOParams(&tt.inCliOpts)
		if tt.errMsg != "" {
			ExpectError(t, err, tt.errMsg, tt.desc)
			// when mount check fails, verify looking in parent dir,
			// verify captured commands match those expected
			if len(tt.expCmds) != len(commands) {
				t.Fatalf(
					"%s: unexpected commands got %#v, want %#v",
					tt.desc, commands, tt.expCmds)
			}
			if len(commands) != 0 {
				AssertEqual(
					t, commands, tt.expCmds, tt.desc+": cmds don't match "+tt.errMsg)
			}
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
		if len(commands) != tt.expNumCmds {
			t.Fatalf(
				"%s: unexpected commands (%#v), expecting %d cmds",
				tt.desc, commands, tt.expNumCmds)
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
			newDefaultMockConfig(),
			0,
			[]string{},
			[]string{},
			false,
			"empty config (no envs) and getenv returns empty",
		},
		{
			newDefaultMockConfig(),
			0,
			[]string{"FOO=bar"},
			[]string{"FOO=bar"},
			false,
			"empty config (no envs) and getenv returns empty",
		},
		{
			populateMockConfig(t, newDefaultMockConfig(), socketsExample),
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
			populateMockConfig(t, newDefaultMockConfig(), psm2Example),
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
		setupTest(t)
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
