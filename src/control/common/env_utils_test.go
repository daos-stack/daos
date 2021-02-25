//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package common

import (
	"os"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func resetEnv(t *testing.T) func() {
	t.Helper()

	startEnv := os.Environ()
	return func() {
		os.Clearenv()
		setEnv(t, startEnv)
	}
}

func setEnv(t *testing.T, env []string) {
	t.Helper()

	for _, keyVal := range env {
		fields := strings.SplitN(keyVal, "=", 2)
		if len(fields) != 2 {
			t.Fatalf("malformed env keyVal %q", keyVal)
		}
		if err := os.Setenv(fields[0], fields[1]); err != nil {
			t.Fatal(err)
		}
	}
}

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
			defer resetEnv(t)()
			os.Clearenv()

			if tc.setup == nil {
				tc.setup = defaultSetup
			}
			if tc.expected == nil {
				tc.expected = defaultSetup
			}
			setEnv(t, tc.setup)

			if tc.whitelist {
				ScrubEnvironmentExcept(tc.list)
			} else {
				ScrubEnvironment(tc.list)
			}

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
		"disabled set to bananas": {
			setup:    proxyEnv,
			disable:  "bananas",
			expected: cleanEnv,
		},
	} {
		t.Run(name, func(t *testing.T) {
			defer resetEnv(t)()
			os.Clearenv()

			if tc.disable != "" {
				os.Setenv(DisableProxyScrubEnv, tc.disable)
				tc.expected = append([]string{DisableProxyScrubEnv + "=" + tc.disable}, tc.expected...)
			}
			setEnv(t, tc.setup)

			ScrubProxyVariables()

			if diff := cmp.Diff(tc.expected, os.Environ()); diff != "" {
				t.Fatalf("unexpected environment (-want, +got):\n%s\n", diff)
			}
		})
	}
}
