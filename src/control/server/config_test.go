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
	"fmt"
	"testing"

	. "utils/handlers"
	. "utils/test"
)

var (
	tmpIn  = "testdata/.tmp_in.yml"
	tmpOut = "testdata/.tmp_out.yml"
)

// TestParseConfigSucceed verified expected input yaml configs match expected output
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
		config, err := loadConfig(tmpIn)
		if err != nil {
			t.Fatal(err.Error())
		}
		// encode decoded config to temporary output file
		err = saveConfig(*config, tmpOut)
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
		AssertEqual(
			t, outputs[0], outputYamls[i],
			fmt.Sprintf(
				"parsed input %d doesn't match expected output:\n\thave %#v\n\twant %#v\n from input %#v\n",
				i, outputs[0], outputYamls[i], y))
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

		_, err = loadConfig(tmpIn)
		// output error messages will always be first entry in a slice.
		ExpectError(t, err, outputErrorMsgs[i][0])
	}
}

// TestProvidedConfig verifies that the provided server config matches what we expect
// after being decoded.
func TestProvidedConfig(t *testing.T) {
	inFiles := []string{
		"../../../utils/config/daos_server.yml", "testdata/daos_server_uncomment.yml"}
	outFiles := []string{"testdata/daos_server.out", "testdata/daos_server_uncomment.out"}

	for i, path := range inFiles {
		config, err := loadConfig(path)
		if err != nil {
			t.Fatal(err.Error())
		}
		// encode decoded config to temporary output file
		err = saveConfig(*config, tmpOut)
		if err != nil {
			t.Fatal(err.Error())
		}
		// use SplitFile (just for convenience) to read output file contents
		outYamls, err := SplitFile(tmpOut)
		if err != nil {
			t.Fatal(err.Error())
		}
		// compare decoded and encoded output (first element of output from SplitFile)
		// matches expected encoded yaml
		pFixture := outFiles[i]
		// use SplitFile (just for convenience) to read fixture ile contents
		outExpectYamls, err := SplitFile(pFixture)
		if err != nil {
			t.Fatal(err.Error())
		}

		AssertEqual(
			t, outYamls[0], outExpectYamls[0],
			fmt.Sprintf(
				"parsed daos_server.yml config doesn't match fixture %s:\n\thave %#v\n\twant %#v\n",
				pFixture, outYamls[0], outExpectYamls[0]))
	}
}
