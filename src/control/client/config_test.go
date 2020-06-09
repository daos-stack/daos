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

package client_test

import (
	"fmt"
	"io/ioutil"
	"os"
	"path"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/client"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

func getDefaultConfig(t *testing.T) (*client.Configuration, *os.File, func()) {
	t.Helper()

	defaultConfig := client.NewConfiguration()
	newPath, err := common.GetAdjacentPath(defaultConfig.Path)
	if err != nil {
		t.Fatal(err)
	}
	defaultConfig.Path = newPath

	// create default config file
	if err := os.MkdirAll(path.Dir(defaultConfig.Path), 0755); err != nil {
		t.Fatal(err)
	}
	f, err := os.Create(defaultConfig.Path)
	if err != nil {
		os.RemoveAll(path.Dir(defaultConfig.Path))
		t.Fatal(err)
	}
	cleanup := func() {
		os.RemoveAll(path.Dir(defaultConfig.Path))
	}

	return defaultConfig, f, cleanup
}

func getTestFile(t *testing.T) *os.File {
	t.Helper()

	testFile, err := ioutil.TempFile("", t.Name())
	if err != nil {
		t.Fatal(err)
	}

	return testFile
}

func TestLoadConfigDefaultsNoFile(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	defaultConfig, f, cleanup := getDefaultConfig(t)
	f.Close()
	defer cleanup()

	cfg, err := client.GetConfig(log, "")
	if err != nil {
		t.Fatal(err)
	}

	if diff := cmp.Diff(fmt.Sprintf("%+v", defaultConfig),
		fmt.Sprintf("%+v", cfg)); diff != "" {

		t.Fatalf("loaded config doesn't match default config (-want, +got):\n%s\n", diff)
	}
}

func TestLoadConfigFromDefaultFile(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	defaultConfig, f, cleanup := getDefaultConfig(t)
	defer cleanup()

	defaultConfig.SystemName = t.Name()

	_, err := f.WriteString(fmt.Sprintf("name: %s\n", t.Name()))
	if err != nil {
		t.Fatal(err)
	}
	f.Close()

	cfg, err := client.GetConfig(log, "")
	if err != nil {
		t.Fatal(err)
	}

	if diff := cmp.Diff(fmt.Sprintf("%+v", defaultConfig),
		fmt.Sprintf("%+v", cfg)); diff != "" {

		t.Fatalf("loaded config doesn't match default config (-want, +got):\n%s\n", diff)
	}
}

func TestLoadConfigFromFile(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	testFile := getTestFile(t)
	defer os.Remove(testFile.Name())

	_, err := testFile.WriteString(fmt.Sprintf("name: %s\n", t.Name()))
	if err != nil {
		t.Fatal(err)
	}
	testFile.Close()

	cfg, err := client.GetConfig(log, testFile.Name())
	if err != nil {
		t.Fatal(err)
	}

	common.AssertEqual(t, cfg.SystemName, t.Name(),
		"loaded config doesn't match written config")
}

func TestLoadConfigFailures(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	testFile := getTestFile(t)
	defer os.Remove(testFile.Name())

	_, err := testFile.WriteString("fneep blip blorp\nquack moo\n")
	if err != nil {
		t.Fatal(err)
	}
	testFile.Close()

	t.Run("unparsable file", func(t *testing.T) {
		_, err := client.GetConfig(log, testFile.Name())
		if err == nil {
			t.Fatal("Expected GetConfig() to fail on unparsable file")
		}
	})

	if err := os.Chmod(testFile.Name(), 0000); err != nil {
		t.Fatal(err)
	}

	t.Run("unreadable file", func(t *testing.T) {
		_, err := client.GetConfig(log, testFile.Name())
		if err == nil {
			t.Fatal("Expected GetConfig() to fail on unreadable file")
		}
	})

	t.Run("nonexistent file", func(t *testing.T) {
		_, err := client.GetConfig(log, "/this/is/a/bad/path.yml")
		if err == nil {
			t.Fatal("Expected GetConfig() to fail on nonexistent file")
		}
	})
}
