//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ui_test

import (
	"errors"
	"sync"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/lib/ui"
	"github.com/daos-stack/daos/src/control/system"
)

func TestUI_RankSetFlag(t *testing.T) {
	for name, tc := range map[string]struct {
		arg       string
		expFlag   *ui.RankSetFlag
		isEmpty   bool
		expString string
		expErr    error
	}{
		"unset": {
			expFlag: &ui.RankSetFlag{},
			isEmpty: true,
		},
		"bad range": {
			arg:    "1-2-3",
			expErr: errors.New("1-2-3"),
		},
		"hostlist whoops": {
			arg:    "foo[1-2]",
			expErr: errors.New("foo[1-2]"),
		},
		"good range": {
			arg: "1-128",
			expFlag: func() *ui.RankSetFlag {
				flag := &ui.RankSetFlag{}
				flag.Replace(system.MustCreateRankSet("1-128"))
				return flag
			}(),
			expString: "1-128",
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := ui.RankSetFlag{}
			gotErr := f.UnmarshalFlag(tc.arg)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, tc.isEmpty, f.Empty(), "unexpected Empty()")
			test.AssertEqual(t, tc.expString, f.String(), "unexpected String()")

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreUnexported(
					ui.RankSetFlag{},
					system.RankSet{},
				),
			}
			if diff := cmp.Diff(tc.expFlag, &f, cmpOpts...); diff != "" {
				t.Fatalf("unexpected flag value: (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestUI_HostSetFlag(t *testing.T) {
	for name, tc := range map[string]struct {
		arg       string
		expFlag   *ui.HostSetFlag
		isEmpty   bool
		expString string
		expErr    error
	}{
		"unset": {
			expFlag: &ui.HostSetFlag{},
			isEmpty: true,
		},
		"bad range": {
			arg:    "host-[1-2-3]",
			expErr: errors.New("1-2-3"),
		},
		"ranklist whoops": {
			arg:    "[1-2]",
			expErr: errors.New("[1-2]"),
		},
		"good list": {
			arg: "host-[1-128]",
			expFlag: func() *ui.HostSetFlag {
				flag := &ui.HostSetFlag{}
				flag.Replace(hostlist.MustCreateSet("host-[1-128]"))
				return flag
			}(),
			expString: "host-[1-128]",
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := ui.HostSetFlag{}
			gotErr := f.UnmarshalFlag(tc.arg)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, tc.isEmpty, f.Empty(), "unexpected Empty()")
			test.AssertEqual(t, tc.expString, f.String(), "unexpected String()")

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreUnexported(
					ui.HostSetFlag{},
					hostlist.HostSet{},
					sync.Mutex{},
				),
			}
			if diff := cmp.Diff(tc.expFlag, &f, cmpOpts...); diff != "" {
				t.Fatalf("unexpected flag value: (-want, +got)\n%s\n", diff)
			}
		})
	}
}
