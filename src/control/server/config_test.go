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

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	. "github.com/daos-stack/daos/src/control/common"
)

const (
	// sConfigUncomment created on init
	sConfigUncomment = "testdata/.daos_server_uncomment.yml"
	socketsExample   = "../../../utils/config/examples/daos_server_sockets.yml"
	psm2Example      = "../../../utils/config/examples/daos_server_psm2.yml"
	defaultConfig    = "../../../utils/config/daos_server.yml"
	tmpIn            = "testdata/.tmp_in.yml"
	tmpOut           = "testdata/.tmp_out.yml"
)

// init gets called once per package, don't call in other test files
func init() {
	// load uncommented version of canonical config file daos_server.yml
	uncommentServerConfig()
}

// uncommentServerConfig removes leading comment chars from daos_server.yml
// lines in order to verify parsing of all available params.
func uncommentServerConfig() {
	fail := func(e error) {
		fmt.Printf("removing comments from server config failed: " + e.Error())
		os.Exit(1)
	}

	cmd := exec.Command(
		"bash", "-c", fmt.Sprintf("sed s/^#//g %s > %s", defaultConfig, sConfigUncomment))

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

// defaultMoc2kConfig returns configuration populated from blank config file
// with mocked external interface.
func defaultMockConfig(t *testing.T) *Configuration {
	return mockConfigFromFile(t, defaultMockExt(), socketsExample)
}

// supply mock external interface, populates config from given file path
func mockConfigFromFile(t *testing.T, e External, path string) *Configuration {
	c := newDefaultConfiguration(e)

	c.Path = path

	err := c.Load()
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
	defer common.ShowLogOnFailure(t)()

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
		config := mockConfigFromFile(t, defaultMockExt(), tmpIn)

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
	defer common.ShowLogOnFailure(t)()

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

		config := defaultMockConfig(t)
		config.Path = tmpIn
		err = config.Load()

		// output error messages will always be first entry in a slice.
		ExpectError(t, err, outputErrorMsgs[i][0], "")
	}
}

// TestProvidedConfigs verifies that the provided server config matches what we expect
// after being decoded.
func TestProvidedConfigs(t *testing.T) {
	defer common.ShowLogOnFailure(t)()

	tests := []struct {
		inExt  External
		inPath string
		desc   string
		errMsg string
	}{
		{
			defaultMockExt(),
			sConfigUncomment,
			"uncommented default config",
			"",
		},
		{
			defaultMockExt(),
			socketsExample,
			"socket example config",
			"",
		},
		{
			defaultMockExt(),
			psm2Example,
			"psm2 example config",
			"",
		},
		{
			defaultMockExt(),
			defaultConfig,
			"default empty config",
			msgBadConfig + relConfExamplesPath + ": " + msgConfigNoProvider,
		},
	}

	for _, tt := range tests {
		// Compare YAML parsing

		// compute file path containing expected output to compare with
		// some input files may be auto generated and prefixed with "."
		// the relevant expect files will not be so strip here
		filename := strings.TrimPrefix(filepath.Base(tt.inPath), ".")
		expectedFile := fmt.Sprintf("testdata/expect_%s", filename)

		config := mockConfigFromFile(t, tt.inExt, tt.inPath)

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

		// verify the generated yaml is of expected size
		if len(outExpectYaml) != len(outYaml) {
			t.Fatalf("number of lines unexpected in %s", expectedFile)
		}

		// Compare IO parameters

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

		err = config.SetIOServerFlags()
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
		var servers []string
		for _, srv := range config.Servers {
			servers = append(servers, fmt.Sprintf("%+v", *srv))
		}
		actual := strings.Split(fmt.Sprintf("%+v", servers), " ")
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

func TestPopulateEnv(t *testing.T) {
	defer common.ShowLogOnFailure(t)()

	noOfiPortConfig := func() *Configuration {
		c := mockConfigFromFile(t, defaultMockExt(), socketsExample)
		c.Servers[0].FabricIfacePort = 0

		return c
	}()

	tests := []struct {
		inConfig *Configuration
		ioIdx    int
		inEnvs   []string
		outEnvs  []string
		errMsg   string
		desc     string
	}{
		{
			mockConfigFromFile(t, defaultMockExt(), defaultConfig),
			0,
			[]string{"FOO=bar"},
			[]string{"FOO=bar"},
			msgConfigNoProvider,
			"empty config (no envs)",
		},
		{
			mockConfigFromFile(t, defaultMockExt(), socketsExample),
			0,
			[]string{"FOO=bar"},
			[]string{
				"FOO=bar",
				"DAOS_MD_CAP=1024",
				"CRT_CTX_SHARE_ADDR=0",
				"CRT_TIMEOUT=30",
				"FI_SOCKETS_MAX_CONN_RETRY=1",
				"FI_SOCKETS_CONN_TIMEOUT=2000",
				"CRT_PHY_ADDR_STR=ofi+sockets",
				"OFI_INTERFACE=eth0",
				"D_LOG_MASK=ERR",
				"D_LOG_FILE=/tmp/server.log",
				"OFI_PORT=31416",
			},
			"",
			"sockets populated config (with envs)",
		},
		{
			mockConfigFromFile(t, defaultMockExt(), socketsExample),
			0,
			// config values should be ignored overwritten from
			// input env
			[]string{
				"FOO=bar",
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
				"DAOS_MD_CAP=1024",
				"CRT_CTX_SHARE_ADDR=0",
				"CRT_TIMEOUT=30",
				"FI_SOCKETS_MAX_CONN_RETRY=1",
				"FI_SOCKETS_CONN_TIMEOUT=2000",
				"CRT_PHY_ADDR_STR=ofi+sockets",
				"OFI_INTERFACE=eth0",
				"D_LOG_MASK=ERR",
				"D_LOG_FILE=/tmp/server.log",
				"OFI_PORT=31416",
			},
			"",
			"sockets populated config (with envs) overwriting pre-existing values",
		},
		{
			mockConfigFromFile(t, defaultMockExt(), psm2Example),
			0,
			[]string{"FOO=bar"},
			[]string{
				"FOO=bar",
				"DAOS_MD_CAP=1024",
				"CRT_CTX_SHARE_ADDR=0",
				"CRT_TIMEOUT=30",
				"CRT_CREDIT_EP_CTX=0",
				"CRT_PHY_ADDR_STR=ofi+psm2",
				"OFI_INTERFACE=ib0",
				"D_LOG_MASK=ERR",
				"D_LOG_FILE=/tmp/server.log",
				"OFI_PORT=31416",
			},
			"",
			"psm2 populated config (with envs)",
		},
		{
			mockConfigFromFile(t, defaultMockExt(), psm2Example),
			0,
			// config values should be ignored overwritten from
			// input env
			[]string{
				"FOO=bar",
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
				"FI_SOCKETS_MAX_CONN_RETRY=somevalue",
				"FI_SOCKETS_CONN_TIMEOUT=somevalue",
				"DAOS_MD_CAP=1024",
				"CRT_CTX_SHARE_ADDR=0",
				"CRT_TIMEOUT=30",
				"CRT_CREDIT_EP_CTX=0",
				"CRT_PHY_ADDR_STR=ofi+psm2",
				"OFI_INTERFACE=ib0",
				"D_LOG_MASK=ERR",
				"D_LOG_FILE=/tmp/server.log",
				"OFI_PORT=31416",
			},
			"",
			"psm2 populated config (with envs) overwriting pre-existing values",
		},
		{
			noOfiPortConfig,
			0,
			[]string{"FOO=bar"},
			[]string{
				"FOO=bar",
				"DAOS_MD_CAP=1024",
				"CRT_CTX_SHARE_ADDR=0",
				"CRT_TIMEOUT=30",
				"FI_SOCKETS_MAX_CONN_RETRY=1",
				"FI_SOCKETS_CONN_TIMEOUT=2000",
				"CRT_PHY_ADDR_STR=ofi+sockets",
				"OFI_INTERFACE=eth0",
				"D_LOG_MASK=ERR",
				"D_LOG_FILE=/tmp/server.log",
				//"OFI_PORT=31416", // not set if not provided via config file
			},
			"",
			"sockets populated config (with envs) but no fabric interface port provided",
		},
	}

	for _, tt := range tests {
		config := tt.inConfig
		inEnvs := tt.inEnvs

		if err := config.Validate(); err != nil {
			if tt.errMsg != "" {
				AssertEqual(
					t, err.Error(), tt.errMsg,
					"unexpected error")
				continue
			}
			t.Fatalf("validate config (%s: %s)", tt.desc, err)
		}

		if err := config.SetIOServerFlags(); err != nil {
			t.Fatalf("Params could not be generated (%s: %s)", tt.desc, err)
		}

		// pass in env and verify output envs is as expected
		config.populateEnv(tt.ioIdx, &inEnvs)
		AssertEqual(t, inEnvs, tt.outEnvs, tt.desc)
	}
}
