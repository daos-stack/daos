//
// (C) Copyright 2018-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"bytes"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	. "github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
)

func TestFlattenAddrs(t *testing.T) {
	for name, tc := range map[string]struct {
		addrPatterns string
		expAddrs     string
		expErrMsg    string
	}{
		"single addr": {
			addrPatterns: "abc:10000",
			expAddrs:     "abc:10000",
		},
		"multiple nodesets": {
			addrPatterns: "abc[1-5]:10000,abc[6-10]:10001,def[1-3]:10000",
			expAddrs:     "abc10:10001,abc1:10000,abc2:10000,abc3:10000,abc4:10000,abc5:10000,abc6:10001,abc7:10001,abc8:10001,abc9:10001,def1:10000,def2:10000,def3:10000",
		},
		"multiple nodeset ranges": {
			addrPatterns: "abc[1-5,7-10],def[1-3,5,7-9]:10000",
			expAddrs:     "abc10:9999,abc1:9999,abc2:9999,abc3:9999,abc4:9999,abc5:9999,abc7:9999,abc8:9999,abc9:9999,def1:10000,def2:10000,def3:10000,def5:10000,def7:10000,def8:10000,def9:10000",
		},
		"multiple ip sets": {
			addrPatterns: "10.0.0.[1-5]:10000,10.0.0.[6-10]:10001,192.168.0.[1-3]:10000",
			expAddrs:     "10.0.0.10:10001,10.0.0.1:10000,10.0.0.2:10000,10.0.0.3:10000,10.0.0.4:10000,10.0.0.5:10000,10.0.0.6:10001,10.0.0.7:10001,10.0.0.8:10001,10.0.0.9:10001,192.168.0.1:10000,192.168.0.2:10000,192.168.0.3:10000",
		},
		"missing port": {
			addrPatterns: "localhost:10001,abc-[1-3]",
			expAddrs:     "abc-1:9999,abc-2:9999,abc-3:9999,localhost:10001",
		},
		"too many colons":     {"bad:addr:here", "", "address bad:addr:here: too many colons in address"},
		"no host":             {"valid:10001,:100", "", "invalid hostname \":100\""},
		"bad host number":     {"1001", "", "invalid hostname \"1001\""},
		"bad port alphabetic": {"foo:bar", "", "invalid port \"bar\""},
		"bad port empty":      {"foo:", "", "invalid port \"\""},
	} {
		t.Run(name, func(t *testing.T) {
			outAddrs, err := flattenHostAddrs(tc.addrPatterns, 9999)
			if err != nil {
				ExpectError(t, err, tc.expErrMsg, name)
				return
			}

			if diff := cmp.Diff(strings.Split(tc.expAddrs, ","), outAddrs); diff != "" {
				t.Fatalf("unexpected output (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func mockHostGroups(t *testing.T) hostlist.HostGroups {
	groups := make(hostlist.HostGroups)

	for k, v := range map[string]string{
		"host1": "13GB (3 devices)/200TB (4 devices)",
		"host2": "13GB (3 devices)/200TB (4 devices)",
		"host3": "13GB (3 devices)/400TB (4 devices)",
		"host4": "13GB (3 devices)/400TB (4 devices)",
		"host5": "10GB (2 devices)/200TB (1 devices)",
	} {
		if err := groups.AddHost(v, k); err != nil {
			t.Fatal("couldn't add host group")
		}
	}

	return groups
}

func TestFormatHostGroups(t *testing.T) {
	for name, tt := range map[string]struct {
		g   hostlist.HostGroups
		out string
	}{
		"formatted results": {
			g:   mockHostGroups(t),
			out: "-----\nhost5\n-----\n10GB (2 devices)/200TB (1 devices)---------\nhost[1-2]\n---------\n13GB (3 devices)/200TB (4 devices)---------\nhost[3-4]\n---------\n13GB (3 devices)/400TB (4 devices)",
		},
	} {
		t.Run(name, func(t *testing.T) {
			buf := &bytes.Buffer{}
			if diff := cmp.Diff(tt.out, formatHostGroups(buf, tt.g)); diff != "" {
				t.Fatalf("unexpected output (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestDmg_errIncompatFlags(t *testing.T) {
	for name, tc := range map[string]struct {
		base     string
		incompat []string
		expErr   error
	}{
		"0 incompat": {
			base:   "base",
			expErr: errors.New("--base may not be mixed"),
		},
		"one incompat": {
			base:     "base",
			incompat: []string{"one"},
			expErr:   errors.New("--base may not be mixed with --one"),
		},
		"two incompat": {
			base:     "base",
			incompat: []string{"one", "two"},
			expErr:   errors.New("--base may not be mixed with --one or --two"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotErr := errIncompatFlags(tc.base, tc.incompat...)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}
