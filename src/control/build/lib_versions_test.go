//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package build

import (
	"io"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
)

func TestBuild_readMappedLibPath(t *testing.T) {
	testMap := `
55a05b000000-55a05b008000 r-xp 00000000 fd:01 44060915                   /usr/bin/cat
55a05b207000-55a05b208000 r--p 00007000 fd:01 44060915                   /usr/bin/cat
55a05b208000-55a05b209000 rw-p 00008000 fd:01 44060915                   /usr/bin/cat
55a05d1d5000-55a05d1f6000 rw-p 00000000 00:00 0                          [heap]
7f2126a00000-7f2126bcd000 r-xp 00000000 fd:01 44043909                   /usr/lib64/libc-2.28.so
7f2126bcd000-7f2126dcc000 ---p 001cd000 fd:01 44043909                   /usr/lib64/libc-2.28.so
7f2126dcc000-7f2126dd0000 r--p 001cc000 fd:01 44043909                   /usr/lib64/libc-2.28.so
7f2126dd0000-7f2126dd2000 rw-p 001d0000 fd:01 44043909                   /usr/lib64/libc-2.28.so
7f2126dd2000-7f2126dd6000 rw-p 00000000 00:00 0
7f2126e00000-7f2126e2f000 r-xp 00000000 fd:01 44043895                   /usr/lib64/ld-2.28.so
7f212702f000-7f2127030000 r--p 0002f000 fd:01 44043895                   /usr/lib64/ld-2.28.so
7f2127030000-7f2127032000 rw-p 00030000 fd:01 44043895                   /usr/lib64/ld-2.28.so
7f2127135000-7f212715a000 rw-p 00000000 00:00 0
7f2127162000-7f2127164000 rw-p 00000000 00:00 0
7f2127164000-7f2127168000 r--p 00000000 00:00 0                          [vvar]
7f2127168000-7f212716a000 r-xp 00000000 00:00 0                          [vdso]
7fffab3d9000-7fffab3fb000 rw-p 00000000 00:00 0                          [stack]
`

	for name, tc := range map[string]struct {
		reader  io.Reader
		libName string
		expPath string
		expErr  error
	}{
		"empty": {},
		"nonexistent": {
			reader:  strings.NewReader(testMap),
			libName: "libmystery",
			expErr:  errors.New("unable to find"),
		},
		"dupes": {
			reader: strings.NewReader(testMap + `
7f2127030000-7f2127032000 rw-p 00030000 fd:01 44043895                   /usr/lib64/libwhoops-2.29.so
7f2127030000-7f2127032000 rw-p 00030000 fd:01 44043895                   /usr/lib64/libwhoops-2.28.so
`),
			libName: "libwhoops",
			expErr:  errors.New("multiple paths"),
		},
		"libc": {
			reader:  strings.NewReader(testMap),
			libName: "libc",
			expPath: "/usr/lib64/libc-2.28.so",
		},
		"libunder_score": {
			reader: strings.NewReader(testMap + `
7f2127030000-7f2127032000 rw-p 00030000 fd:01 44043895                   /usr/lib64/libkool_wow.so.123
7f2127030000-7f2127032000 rw-p 00030000 fd:01 44043895                   /usr/lib64/libkool.so.456
`),
			libName: "libkool",
			expPath: "/usr/lib64/libkool.so.456",
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotPath, gotErr := readMappedLibPath(tc.reader, tc.libName)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expPath, gotPath); diff != "" {
				t.Fatalf("unexpected path for %q (-want,+got): %s", tc.libName, diff)
			}
		})
	}
}
