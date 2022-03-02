//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"fmt"
	"io/ioutil"
	"os"
	"path"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"
	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/security"
)

var (
	testCfg       = DefaultConfig()
	defCfgCmpOpts = []cmp.Option{
		cmpopts.IgnoreUnexported(security.CertificateConfig{}),
	}
)

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
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	restore := setDirs(t, "NONE", tmpDir)
	defer restore(t)
	saveConfig(t, testCfg, SystemConfigPath())
	testCfg.Path = fmt.Sprintf("%s/%s", tmpDir, defaultConfigFile)

	gotCfg, err := LoadConfig("")
	if err != nil {
		t.Fatal(err)
	}

	if diff := cmp.Diff(testCfg, gotCfg, defCfgCmpOpts...); diff != "" {
		t.Fatalf("loaded cfg doesn't match (-want, +got):\n%s\n", diff)
	}
}

func TestControl_LoadUserConfig(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	restore := setDirs(t, tmpDir, "NONE")
	defer restore(t)
	saveConfig(t, testCfg, UserConfigPath())
	testCfg.Path = fmt.Sprintf("%s/.%s", tmpDir, defaultConfigFile)

	gotCfg, err := LoadConfig("")
	if err != nil {
		t.Fatal(err)
	}

	if diff := cmp.Diff(testCfg, gotCfg, defCfgCmpOpts...); diff != "" {
		t.Fatalf("loaded cfg doesn't match (-want, +got):\n%s\n", diff)
	}
}

func TestControl_LoadSpecifiedConfig(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	restore := setDirs(t, "NONE", "NONE")
	defer restore(t)
	testPath := path.Join(tmpDir, "test.yml")
	saveConfig(t, testCfg, testPath)
	testCfg.Path = testPath

	gotCfg, err := LoadConfig(testPath)
	if err != nil {
		t.Fatal(err)
	}

	if diff := cmp.Diff(testCfg, gotCfg, defCfgCmpOpts...); diff != "" {
		t.Fatalf("loaded cfg doesn't match (-want, +got):\n%s\n", diff)
	}
}

func TestControl_LoadConfig_NoneFound(t *testing.T) {
	restore := setDirs(t, "NONE", "NONE")
	defer restore(t)

	gotCfg, err := LoadConfig("")
	test.CmpErr(t, err, ErrNoConfigFile)
	if gotCfg != nil {
		t.Fatalf("got non-nil cfg, want nil")
	}
}

func TestControl_LoadConfig_BadInputs(t *testing.T) {
	for name, tc := range map[string]struct {
		input  string
		expErr error
	}{
		"empty": {
			input:  "",
			expErr: errors.New("empty config file"),
		},
		"bad hostlist": {
			input:  `hostlist: ['nvm0612-ib0:10001','nvm0611-ib0:10001,'nvm0610-ib0:10001']`,
			expErr: errors.New("did not find expected"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			tmpDir, cleanup := test.CreateTestDir(t)
			defer cleanup()
			tmpPath := path.Join(tmpDir, defaultConfigFile)
			f, err := os.Create(tmpPath)
			if err != nil {
				t.Fatal(err)
			}
			_, err = f.WriteString(tc.input)
			if err != nil {
				t.Fatal(err)
			}
			f.Close()

			cfg, err := LoadConfig(tmpPath)
			test.CmpErr(t, tc.expErr, err)
			if cfg != nil {
				t.Fatalf("got non-nil cfg, want nil")
			}
		})
	}
}
