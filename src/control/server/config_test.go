//
// (C) Copyright 2018 Intel Corporation.
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
	"errors"
	"fmt"
	"io/ioutil"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	. "github.com/daos-stack/daos/src/control/utils/handlers"
	. "github.com/daos-stack/daos/src/control/utils/test"
)

var (
	sConfig = "../../../utils/config/daos_server.yml"
	// sConfigUncomment not written before tests start
	sConfigUncomment   = "testdata/.daos_server_uncomment.yml"
	exampleConfigFiles = []string{
		"../../../utils/config/daos_server.yml",
		"../../../utils/config/examples/daos_server_sockets.yml",
		"../../../utils/config/examples/daos_server_psm2.yml",
	}
	tmpIn    = "testdata/.tmp_in.yml"
	tmpOut   = "testdata/.tmp_out.yml"
	testInit = 0
)

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
		fmt.Printf("%s", slurp)

		if err := cmd.Wait(); err != nil {
			t.Fatal(err)
		}
		testInit = 1
		// add written configUncomment to list of example config files to test
		exampleConfigFiles = append(exampleConfigFiles, sConfigUncomment)
	}
}

type mockExt struct {
	checkMountRet error
	getenvRet     string
}

func (m *mockExt) checkMount(path string) error { return m.checkMountRet }
func (m *mockExt) getenv(key string) string     { return m.getenvRet }

// NewMockConfig returns default configuration with mocked external interface
func NewMockConfig(mountCheckRet error, envCheckRet string) configuration {
	return NewDefaultConfiguration(&mockExt{mountCheckRet, envCheckRet})
}

// NewDefaultMockConfig returns MockConfig with default mock return values
func NewDefaultMockConfig() configuration {
	return NewMockConfig(nil, "somevalue")
}

// TestParseConfigSucceed verifies expected input yaml configs match expected output
// yaml after being encoded and decoded. Input and expected output combinations read
// from 2 files with multiple entries.
// Write input to file, loadConfig (decode), saveConf (encode) and compare written yaml.
func TestParseConfigSucceed(t *testing.T) {
	inputYamls, outputYamls, err := LoadTestFiles(
		"testdata/input_good.txt", "testdata/output_success.txt")
	if err != nil {
		t.Fatal(err.Error())
	}

	for i, y := range inputYamls {
		// write input yaml config to temporary file
		err := WriteSlice(tmpIn, y)
		if err != nil {
			t.Fatal(err.Error())
		}
		// verify decoding of config from written file
		config := NewDefaultMockConfig()
		config.Path = tmpIn
		err = config.loadConfig()
		if err != nil {
			t.Fatal(err.Error())
		}
		// encode decoded config to temporary output file
		err = config.saveConfig(tmpOut)
		if err != nil {
			t.Fatal(err.Error())
		}
		// use SplitFile (just for convenience) to read output file contents
		outputs, err := SplitFile(tmpOut)
		if err != nil {
			t.Fatal(err.Error())
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
		t.Fatal(err.Error())
	}

	for i, y := range inputYamls {
		err := WriteSlice(tmpIn, y)
		if err != nil {
			t.Fatal(err.Error())
		}

		config := NewDefaultMockConfig()
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

	for _, path := range exampleConfigFiles {
		// compute file path containing expected output to compare with
		// some input files may be auto generated and prefixed with "."
		// the relevant expect files will not be so strip here
		filename := strings.TrimPrefix(filepath.Base(path), ".")
		expectedFile := fmt.Sprintf("testdata/expect_%s", filename)
		// decode target config
		config := NewDefaultMockConfig()
		config.Path = path
		err := config.loadConfig()
		if err != nil {
			t.Fatalf("problem when loading %s: %s", path, err.Error())
		}
		// encode decoded config to temporary output file to verify parser
		err = config.saveConfig(tmpOut)
		if err != nil {
			t.Fatal(err.Error())
		}
		// use SplitFile (just for convenience) to read what has been output
		outYamls, err := SplitFile(tmpOut)
		if err != nil {
			t.Fatal(
				fmt.Sprintf(
					"problem reading processed output file %s: %s",
					tmpOut, err.Error()))
			t.Fatal(err.Error())
		}
		// read and compare output file contents with expected file contents
		// (outFile), (extract first element of output from SplitFile for each)
		outExpectYamls, err := SplitFile(expectedFile)
		if err != nil {
			t.Fatal(
				fmt.Sprintf(
					"problem reading expected output file %s: %s",
					expectedFile, err.Error()))
		}

		for i, line := range outExpectYamls[0] {
			AssertEqual(
				t, outYamls[0][i], line,
				fmt.Sprintf(
					"line %d parsed %s config doesn't match fixture %s:\n\thave %#v\n\twant %#v\n",
					i, path, expectedFile, outYamls[0][i], line))
		}

		// compute file path containing expected output to compare with
		expectedFile = fmt.Sprintf(
			"testdata/ioparams_%s.txt", strings.TrimSuffix(filename, filepath.Ext(path)))
		outExpect, err := SplitFile(expectedFile)
		if err != nil {
			t.Fatal(
				fmt.Sprintf(
					"problem reading expected output file %s: %s",
					expectedFile, err.Error()))
		}
		// now verify expected IO server parameters are generated
		err = config.getIOParams(&cliOptions{})
		if err != nil {
			t.Fatalf(
				"problem retrieving IO params using conf %s: %s",
				filename, err.Error())
		}
		// should only ever be one line in expected output
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
			t.Fatal(err.Error())
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
			t.Fatal(err.Error())
		}
		AssertEqual(t, num, tt.num, "failed to convert to expected number")
	}
}

func newC(t *testing.T) configuration {
	config := NewDefaultMockConfig()
	config.Path = sConfigUncomment
	err := config.loadConfig()
	if err != nil {
		t.Fatalf("Configuration could not be read (%s)", err.Error())
	}
	return config
}

// TestCmdlineOverride verified that cliOpts take precedence over existing
// configs resulting in overrides appearing in ioparams
func TestCmdlineOverride(t *testing.T) {
	r := uint(9)
	m := "moduleA moduleB"
	a := "/some/file"
	y := "/another/different/file"

	uncommentServerConfig(t)

	tests := []struct {
		inCliOpts  *cliOptions
		inConfig   configuration
		outCliOpts [][]string
		desc       string
		errMsg     string
	}{
		{
			&cliOptions{},
			newC(t),
			[][]string{
				{"-c", "21", "-g", "daos", "-s", "/mnt/daos/1", "-r", "0"},
				{"-c", "20", "-g", "daos", "-s", "/mnt/daos/2", "-r", "1"},
			},
			"None",
			"",
		},
		{
			&cliOptions{MountPath: "/foo/bar"},
			newC(t),
			[][]string{
				{"-c", "21", "-g", "daos", "-s", "/foo/bar", "-r", "0"},
				{"-c", "20", "-g", "daos", "-s", "/foo/bar", "-r", "1"},
			},
			"MountPath",
			"",
		},
		{
			&cliOptions{Group: "testing123"},
			newC(t),
			[][]string{
				{"-c", "21", "-g", "testing123", "-s", "/mnt/daos/1", "-r", "0"},
				{"-c", "20", "-g", "testing123", "-s", "/mnt/daos/2", "-r", "1"},
			},
			"Group",
			"",
		},
		{
			&cliOptions{Cores: 2},
			newC(t),
			[][]string{
				{"-c", "2", "-g", "daos", "-s", "/mnt/daos/1", "-r", "0"},
				{"-c", "2", "-g", "daos", "-s", "/mnt/daos/2", "-r", "1"},
			},
			"Cores",
			"",
		},
		{
			&cliOptions{Rank: &r},
			newC(t),
			[][]string{
				{"-c", "21", "-g", "daos", "-s", "/mnt/daos/1", "-r", "9"},
				{"-c", "20", "-g", "daos", "-s", "/mnt/daos/2", "-r", "1"},
			},
			"Rank",
			"",
		},
		{
			&cliOptions{SocketDir: "/tmp/Jeremy", Modules: &m, Attach: &a, Map: &y},
			newC(t),
			[][]string{
				{
					"-c", "21", "-g", "daos", "-s", "/mnt/daos/1",
					"-m", "moduleA moduleB", "-a", "/some/file", "-y",
					"/another/different/file", "-r", "0",
				},
				{
					"-c", "20", "-g", "daos", "-s", "/mnt/daos/2",
					"-m", "moduleA moduleB", "-a", "/some/file", "-y",
					"/another/different/file", "-r", "1",
				},
			},
			"SocketDir Modules Attach Map",
			"",
		},
		{
			// no provider set but os env set mock getenv returns not empty string
			&cliOptions{},
			NewDefaultMockConfig(),
			[][]string{
				{"-c", "0", "-g", "daos_server", "-s", "/mnt/daos"},
			},
			"use defaults, no Provider set but provider env exists",
			"",
		},
		{
			// no provider set but os env set mock getenv returns not empty string
			&cliOptions{
				Cores: 2, Group: "bob", MountPath: "/foo/bar",
				SocketDir: "/tmp/Jeremy", Modules: &m, Attach: &a, Map: &y},
			NewDefaultMockConfig(),
			[][]string{
				{
					"-c", "2", "-g", "bob", "-s", "/foo/bar",
					"-m", "moduleA moduleB", "-a", "/some/file", "-y",
					"/another/different/file",
				},
			},
			"override defaults, no Provider set but provider env exists",
			"",
		},
		{
			// no provider set and no os env set mock getenv returns empty string
			&cliOptions{
				Cores: 2, Group: "bob", MountPath: "/foo/bar",
				SocketDir: "/tmp/Jeremy", Modules: &m, Attach: &a, Map: &y},
			NewMockConfig(nil, ""),
			[][]string{
				{
					"-c", "2", "-g", "bob", "-s", "/foo/bar",
					"-m", "moduleA moduleB", "-a", "/some/file", "-y",
					"/another/different/file",
				},
			},
			"override defaults, no Provider set and no provider env exists",
			"required parameters missing from config and os environment (CRT_PHY_ADDR_STR)",
		},
		{
			// no provider set but os env set mock getenv returns not empty string
			// mount not set and returns invalid
			&cliOptions{},
			NewMockConfig(errors.New("exit status 1"), "somevalue"),
			[][]string{},
			"use defaults, mount check fails with default",
			"server0 scm mount path (/mnt/daos) not mounted: exit status 1",
		},
		{
			// no provider set but os env set mock getenv returns not empty string
			// mount set and returns invalid
			&cliOptions{MountPath: "/foo/bar"},
			NewMockConfig(errors.New("exit status 1"), "somevalue"),
			[][]string{},
			"override MountPath, mount check fails with new value",
			"server0 scm mount path (/foo/bar) not mounted: exit status 1",
		},
	}

	for _, tt := range tests {
		config := tt.inConfig
		err := config.getIOParams(tt.inCliOpts)
		if tt.errMsg != "" {
			ExpectError(t, err, tt.errMsg, tt.desc)
			continue
		}
		if err != nil {
			t.Fatalf("Params could not be generated (%s: %s)", tt.desc, err.Error())
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
