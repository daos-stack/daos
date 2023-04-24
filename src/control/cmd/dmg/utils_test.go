//
// (C) Copyright 2018-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"bytes"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
)

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

func TestDmg_singleHostFlag_UnmarshalFlag(t *testing.T) {
	for name, tc := range map[string]struct {
		val    string
		expErr error
	}{
		"empty": {
			expErr: errors.New("single host"),
		},
		"single host": {
			val: "localhost",
		},
		"multi host": {
			val:    "host[1-2]",
			expErr: errors.New("single host"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			flag := new(singleHostFlag)

			err := flag.UnmarshalFlag(tc.val)

			test.CmpErr(t, tc.expErr, err)
			test.AssertEqual(t, tc.val, flag.HostSet.String(), "")
		})
	}
}
