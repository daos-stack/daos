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
	"os"
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
				FaultPath: "/test/fault/path",
			},
			expResult: "/test/fault/path",
		},
		"cfg fault path is not valid": {
			cfg: &Configuration{
				FaultPath: "junk",
			},
			expErr: FaultConfigFaultDomainInvalid,
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
