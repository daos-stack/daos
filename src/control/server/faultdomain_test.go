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

package server

import (
	"fmt"
	"os"
	"path/filepath"
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/system"
)

func TestServer_getDefaultFaultDomain(t *testing.T) {
	for name, tc := range map[string]struct {
		getHostname hostnameGetterFn
		expResult   string
		expErr      error
	}{
		"hostname returns error": {
			getHostname: func() (string, error) {
				return "", errors.New("mock hostname")
			},
			expErr: errors.New("mock hostname"),
		},
		"success": {
			getHostname: func() (string, error) {
				return "myhost", nil
			},
			expResult: "/myhost",
		},
		"hostname not usable": {
			getHostname: func() (string, error) {
				return "/////////", nil
			},
			expErr: FaultConfigFaultDomainInvalid,
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := getDefaultFaultDomain(tc.getHostname)

			common.CmpErr(t, tc.expErr, err)
			common.AssertEqual(t, tc.expResult, result.String(), "incorrect fault domain")
		})
	}
}

func TestServer_getFaultDomain(t *testing.T) {
	realHostname, err := os.Hostname()
	if err != nil {
		t.Fatalf("couldn't get hostname: %s", err)
	}

	tmpDir, cleanup := common.CreateTestDir(t)
	defer cleanup()

	validFaultDomain := "/rack0/pdu1/node1"
	cbScriptPath := filepath.Join(tmpDir, "cb.sh")
	createFaultCBScriptFile(t, cbScriptPath, 0755, validFaultDomain)

	for name, tc := range map[string]struct {
		cfg       *Configuration
		expResult string
		expErr    error
	}{
		"nil cfg": {
			expErr: FaultBadConfig,
		},
		"cfg fault path": {
			cfg: &Configuration{
				FaultPath: validFaultDomain,
			},
			expResult: validFaultDomain,
		},
		"cfg fault path is not valid": {
			cfg: &Configuration{
				FaultPath: "junk",
			},
			expErr: FaultConfigFaultDomainInvalid,
		},
		"cfg fault callback": {
			cfg: &Configuration{
				FaultCb: cbScriptPath,
			},
			expResult: validFaultDomain,
		},
		"cfg fault callback is not valid": {
			cfg: &Configuration{
				FaultCb: filepath.Join(tmpDir, "does not exist"),
			},
			expErr: FaultConfigFaultCallbackNotFound,
		},
		"cfg both fault path and fault CB": {
			cfg: &Configuration{
				FaultPath: validFaultDomain,
				FaultCb:   cbScriptPath,
			},
			expErr: FaultConfigBothFaultPathAndCb,
		},
		"default gets hostname": {
			cfg:       &Configuration{},
			expResult: system.FaultDomainSeparator + realHostname,
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := getFaultDomain(tc.cfg)

			common.CmpErr(t, tc.expErr, err)
			common.AssertEqual(t, tc.expResult, result.String(), "incorrect fault domain")
		})
	}
}

func createFaultCBScriptFile(t *testing.T, path string, mode os.FileMode, output string) {
	t.Helper()

	createScriptFile(t, path, mode, fmt.Sprintf("echo %q\n", output))
}

func createErrorScriptFile(t *testing.T, path string) {
	t.Helper()

	badPath := filepath.Join(path, "invalid") // making an impossible path out of our script's path
	createScriptFile(t, path, 0755, fmt.Sprintf("ls %s\n", badPath))
}

func createScriptFile(t *testing.T, path string, mode os.FileMode, content string) {
	t.Helper()

	f, err := os.Create(path)
	if err != nil {
		t.Fatalf("failed to create script file %q: %s", path, err)
	}

	_, err = f.WriteString(fmt.Sprintf("#!/bin/bash\n\n%s\n", content))
	if err != nil {
		t.Fatalf("failed to write to script file %q: %s", path, err)
	}

	err = f.Chmod(mode)
	if err != nil {
		t.Fatalf("failed to update mode of script file %q to %d: %s", path, mode, err)
	}

	f.Close()
}

func TestServer_getFaultDomainFromCallback(t *testing.T) {
	tmpDir, cleanup := common.CreateTestDir(t)
	defer cleanup()

	validFaultDomain := "/my/fault/domain"

	goodScriptPath := filepath.Join(tmpDir, "good.sh")
	createFaultCBScriptFile(t, goodScriptPath, 0755, validFaultDomain)

	badPermsScriptPath := filepath.Join(tmpDir, "noPerms.sh")
	createFaultCBScriptFile(t, badPermsScriptPath, 0600, validFaultDomain)

	errorScriptPath := filepath.Join(tmpDir, "fail.sh")
	createErrorScriptFile(t, errorScriptPath)

	emptyScriptPath := filepath.Join(tmpDir, "empty.sh")
	createFaultCBScriptFile(t, emptyScriptPath, 0755, "")

	whitespaceScriptPath := filepath.Join(tmpDir, "whitespace.sh")
	createFaultCBScriptFile(t, whitespaceScriptPath, 0755, "     ")

	invalidScriptPath := filepath.Join(tmpDir, "invalid.sh")
	createFaultCBScriptFile(t, invalidScriptPath, 0755, "some junk")

	for name, tc := range map[string]struct {
		input     string
		expResult string
		expErr    error
	}{
		"empty path": {
			expErr: errors.New("no callback path supplied"),
		},
		"success": {
			input:     goodScriptPath,
			expResult: validFaultDomain,
		},
		"script does not exist": {
			input:  filepath.Join(tmpDir, "notarealfile"),
			expErr: FaultConfigFaultCallbackNotFound,
		},
		"script has bad permissions": {
			input:  badPermsScriptPath,
			expErr: FaultConfigFaultCallbackBadPerms,
		},
		"error within the script": {
			input:  errorScriptPath,
			expErr: FaultConfigFaultCallbackFailed(errors.New("exit status 2")),
		},
		"script returned no output": {
			input:  emptyScriptPath,
			expErr: FaultConfigFaultCallbackEmpty,
		},
		"script returned only whitespace": {
			input:  whitespaceScriptPath,
			expErr: FaultConfigFaultCallbackEmpty,
		},
		"script returned invalid fault domain": {
			input:  invalidScriptPath,
			expErr: FaultConfigFaultDomainInvalid,
		},
		"no arbitrary shell commands allowed": {
			input:  "echo \"my dog has fleas\"",
			expErr: FaultConfigFaultCallbackNotFound,
		},
		"no command line parameters allowed": {
			input:  fmt.Sprintf("%s arg", goodScriptPath),
			expErr: FaultConfigFaultCallbackNotFound,
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := getFaultDomainFromCallback(tc.input)

			common.CmpErr(t, tc.expErr, err)
			common.AssertEqual(t, tc.expResult, result.String(), "incorrect fault domain")
		})
	}
}
