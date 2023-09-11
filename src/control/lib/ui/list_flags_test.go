//
// (C) Copyright 2022-2023 Intel Corporation.
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
	"github.com/jessevdk/go-flags"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
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
				flag.Replace(ranklist.MustCreateRankSet("1-128"))
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
					ranklist.RankSet{},
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

func TestUI_MemberStateSetFlag(t *testing.T) {
	for name, tc := range map[string]struct {
		arg     string
		isEmpty bool
		expFlag *ui.MemberStateSetFlag
		expErr  error
	}{
		"unset": {
			isEmpty: true,
			expFlag: &ui.MemberStateSetFlag{},
		},
		"bad state": {
			arg:    "Borked",
			expErr: errors.New("invalid state name"),
		},
		"invalid unknown state": {
			arg:    "Unknown",
			expErr: errors.New("invalid state name"),
		},
		"good list": {
			arg: "Joined,Excluded",
			expFlag: &ui.MemberStateSetFlag{
				States: system.MemberStateJoined | system.MemberStateExcluded,
			},
		},
		"good list; caps insensitive": {
			arg: "JOINED,excluded",
			expFlag: &ui.MemberStateSetFlag{
				States: system.MemberStateJoined | system.MemberStateExcluded,
			},
		},
		"good list; with spaces": {
			arg: "Joined, Excluded",
			expFlag: &ui.MemberStateSetFlag{
				States: system.MemberStateJoined | system.MemberStateExcluded,
			},
		},
		"full list": {
			arg: "Joined,Excluded,Stopped,Stopping,Ready,Starting,AwaitFormat,AdminExcluded,Errored,Unresponsive",
			expFlag: &ui.MemberStateSetFlag{
				States: system.MemberState(int(system.MemberStateMax) - 1),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := ui.MemberStateSetFlag{}
			gotErr := f.UnmarshalFlag(tc.arg)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, tc.isEmpty, f.Empty(), "unexpected Empty()")

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreUnexported(
					ui.MemberStateSetFlag{},
				),
			}
			if diff := cmp.Diff(tc.expFlag, &f, cmpOpts...); diff != "" {
				t.Fatalf("unexpected flag value: (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestUI_MemberStateSetFlag_Complete(t *testing.T) {
	for name, tc := range map[string]struct {
		arg          string
		expComplStrs []string
		expErr       error
	}{
		"empty string; suggest all": {
			expComplStrs: []string{
				"AdminExcluded", "AwaitFormat", "Errored", "Excluded",
				"Joined", "Ready", "Starting", "Stopped", "Stopping",
				"Unresponsive",
			},
		},
		"single suggestion": {
			arg:          "Excl",
			expComplStrs: []string{"Excluded"},
		},
		"multiple suggestions": {
			arg:          "S",
			expComplStrs: []string{"Starting", "Stopped", "Stopping"},
		},
		"multiple suggestions; case insensitive": {
			arg:          "s",
			expComplStrs: []string{"Starting", "Stopped", "Stopping"},
		},
		"suggestions after prefix; empty string; suggest remainder": {
			arg: "Starting,",
			expComplStrs: []string{
				"Starting,AdminExcluded", "Starting,AwaitFormat",
				"Starting,Errored", "Starting,Excluded", "Starting,Joined",
				"Starting,Ready", "Starting,Stopped", "Starting,Stopping",
				"Starting,Unresponsive",
			},
		},
		"suggestions after prefix; partial match": {
			arg:          "Starting,S",
			expComplStrs: []string{"Starting,Stopped", "Starting,Stopping"},
		},
		"suggestions after multiple states in prefix; partial match": {
			arg: "Starting,Stopped,Stopping,Unresponsive,AwaitFormat,E",
			expComplStrs: []string{
				"Starting,Stopped,Stopping,Unresponsive,AwaitFormat,Errored",
				"Starting,Stopped,Stopping,Unresponsive,AwaitFormat,Excluded",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := ui.MemberStateSetFlag{}
			gotComps := f.Complete(tc.arg)

			expComps := make([]flags.Completion, len(tc.expComplStrs))
			for i, s := range tc.expComplStrs {
				expComps[i] = flags.Completion{
					Item: s,
				}
			}

			if diff := cmp.Diff(expComps, gotComps); diff != "" {
				t.Fatalf("unexpected flag value: (-want, +got)\n%s\n", diff)
			}
		})
	}
}
