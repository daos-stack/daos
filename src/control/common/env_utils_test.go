//
// (C) Copyright 2019 Intel Corporation.
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
package common

import (
	"os"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestScrubEnvironment(t *testing.T) {
	defaultSetup := []string{
		"FOO=bar", "baz=quux", "COW=QUACK", "ANSWER=42",
	}
	for name, tc := range map[string]struct {
		setup     []string
		list      []string
		whitelist bool
		expected  []string
	}{
		"nil blacklist": {},
		"empty blacklist": {
			list: []string{},
		},
		"blacklist": {
			list:     []string{"COW", "baz"},
			expected: []string{"FOO=bar", "ANSWER=42"},
		},
		"nil whitelist": {
			whitelist: true,
			expected:  []string{},
		},
		"empty whitelist": {
			list:      []string{},
			whitelist: true,
			expected:  []string{},
		},
		"whitelist": {
			list:      []string{"ANSWER"},
			whitelist: true,
			expected:  []string{"ANSWER=42"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			os.Clearenv()

			if tc.setup == nil {
				tc.setup = defaultSetup
			}
			if tc.expected == nil {
				tc.expected = defaultSetup
			}

			for _, keyVal := range tc.setup {
				fields := strings.SplitN(keyVal, "=", 2)
				if err := os.Setenv(fields[0], fields[1]); err != nil {
					t.Fatal(err)
				}
			}

			ScrubEnvironment(tc.list, tc.whitelist)

			if diff := cmp.Diff(tc.expected, os.Environ()); diff != "" {
				t.Fatalf("unexpected environment (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestScrubProxyVariables(t *testing.T) {
	cleanEnv := []string{
		"FOO=bar", "baz=quux", "COW=QUACK", "ANSWER=42",
	}
	proxyEnv := append(cleanEnv, []string{
		"http_proxy=https://woo.proxy.proxy.proxy:9000/",
		"HTTP_PROXY=uppercase",
		"https_proxy=hi",
		"HTTPS_PROXY=bye",
		"ftp_proxy=lowercase",
		"FTP_PROXY=yep it's a proxy somehow this is weird",
		"no_proxy=nope",
		"NO_PROXY=NOPE",
	}...)
	for name, tc := range map[string]struct {
		setup    []string
		disable  string
		expected []string
	}{
		"clean env (never set)": {
			setup:    cleanEnv,
			expected: cleanEnv,
		},
		"proxy env": {
			setup:    proxyEnv,
			expected: cleanEnv,
		},
		"disabled scrub": {
			setup:    proxyEnv,
			disable:  "1",
			expected: proxyEnv,
		},
		"disabled set to false": {
			setup:    proxyEnv,
			disable:  "0",
			expected: cleanEnv,
		},
	} {
		t.Run(name, func(t *testing.T) {
			os.Clearenv()

			if tc.disable != "" {
				os.Setenv(DisableProxyScrubEnv, tc.disable)
				tc.expected = append([]string{DisableProxyScrubEnv + "=" + tc.disable}, tc.expected...)
			}

			for _, keyVal := range tc.setup {
				fields := strings.SplitN(keyVal, "=", 2)
				if err := os.Setenv(fields[0], fields[1]); err != nil {
					t.Fatal(err)
				}
			}

			ScrubProxyVariables()

			if diff := cmp.Diff(tc.expected, os.Environ()); diff != "" {
				t.Fatalf("unexpected environment (-want, +got):\n%s\n", diff)
			}
		})
	}
}
