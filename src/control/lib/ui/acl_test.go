//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ui_test

import (
	"errors"
	"strings"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ui"
)

func TestUI_ACLPrincipalFlag(t *testing.T) {
	var p ui.ACLPrincipalFlag
	tooLong := strings.Repeat("a", daos.ACLPrincipalMaxLen+1)

	for _, tc := range []struct {
		in     string
		out    string
		expErr error
	}{
		{"user", "user@", nil},
		{"user@", "user@", nil},
		{"user@domain", "user@domain", nil},
		{"user@domain@", "", errors.New("invalid ACL principal \"user@domain@\"")},
		{"@user", "", errors.New("invalid ACL principal \"@user\"")},
		{"@", "", errors.New("invalid ACL principal \"@\"")},
		{tooLong, "", errors.New("invalid ACL principal \"" + tooLong + "\"")},
	} {
		t.Run(tc.in, func(t *testing.T) {
			gotErr := p.UnmarshalFlag(tc.in)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
			if p.String() != tc.out {
				t.Fatalf("got %q, want %q", p, tc.out)
			}
		})
	}
}
