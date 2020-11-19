//
// (C) Copyright 2020 Intel Corporation.
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

package control

import (
	"io/ioutil"
	"os"
	"path"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/security"
)

var testCfg = DefaultConfig()

func saveConfig(t *testing.T, cfg *Config, cfgPath string) {
	t.Helper()

	data, err := yaml.Marshal(cfg)
	if err != nil {
		t.Fatal(err)
	}

	if err := ioutil.WriteFile(cfgPath, data, 0644); err != nil {
		t.Fatal(err)
	}
}

func setDirs(t *testing.T, newHome, newSys string) func(t *testing.T) {
	t.Helper()

	// NB: This is safe because each package's tests are in
	// their own binary, and tests within a package run sequentially.
	curHome, err := os.UserHomeDir()
	if err != nil {
		t.Fatal(err)
	}
	if err := os.Setenv("HOME", newHome); err != nil {
		t.Fatal(err)
	}

	curSys := build.ConfigDir
	build.ConfigDir = newSys

	return func(t *testing.T) {
		_ = setDirs(t, curHome, curSys)
	}
}

func TestControl_LoadSystemConfig(t *testing.T) {
	tmpDir, cleanup := common.CreateTestDir(t)
	defer cleanup()

	restore := setDirs(t, "NONE", tmpDir)
	defer restore(t)
	saveConfig(t, testCfg, SystemConfigPath())

	gotCfg, err := LoadConfig("")
	if err != nil {
		t.Fatal(err)
	}

	cmpOpts := []cmp.Option{
		cmpopts.IgnoreUnexported(security.CertificateConfig{}),
	}
	if diff := cmp.Diff(testCfg, gotCfg, cmpOpts...); diff != "" {
		t.Fatalf("loaded cfg doesn't match (-want, +got):\n%s\n", diff)
	}
}

func TestControl_LoadUserConfig(t *testing.T) {
	tmpDir, cleanup := common.CreateTestDir(t)
	defer cleanup()

	restore := setDirs(t, tmpDir, "NONE")
	defer restore(t)
	saveConfig(t, testCfg, UserConfigPath())

	gotCfg, err := LoadConfig("")
	if err != nil {
		t.Fatal(err)
	}

	cmpOpts := []cmp.Option{
		cmpopts.IgnoreUnexported(security.CertificateConfig{}),
	}
	if diff := cmp.Diff(testCfg, gotCfg, cmpOpts...); diff != "" {
		t.Fatalf("loaded cfg doesn't match (-want, +got):\n%s\n", diff)
	}
}

func TestControl_LoadSpecifiedConfig(t *testing.T) {
	tmpDir, cleanup := common.CreateTestDir(t)
	defer cleanup()

	restore := setDirs(t, "NONE", "NONE")
	defer restore(t)
	testPath := path.Join(tmpDir, "test.yml")
	saveConfig(t, testCfg, testPath)

	gotCfg, err := LoadConfig(testPath)
	if err != nil {
		t.Fatal(err)
	}

	cmpOpts := []cmp.Option{
		cmpopts.IgnoreUnexported(security.CertificateConfig{}),
	}
	if diff := cmp.Diff(testCfg, gotCfg, cmpOpts...); diff != "" {
		t.Fatalf("loaded cfg doesn't match (-want, +got):\n%s\n", diff)
	}
}
