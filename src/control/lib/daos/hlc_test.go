//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos_test

import (
	"testing"
	"time"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
)

func TestDaos_HLC(t *testing.T) {
	now := time.Now().Truncate(0)

	for name, tc := range map[string]struct {
		in      int64
		expDate string
	}{
		"zero": {
			in:      0,
			expDate: "2021-01-01 00:00:00 +0000 UTC", // HLC epoch is 2021-01-01
		},
		"now": {
			in:      now.UnixNano(),
			expDate: now.String(),
		},
	} {
		t.Run(name, func(t *testing.T) {
			hlc := daos.NewHLC(tc.in)
			test.AssertEqual(t, tc.expDate, hlc.String(), "not equal")
		})
	}
}

func TestDaos_HLC_JSON(t *testing.T) {
	now := time.Now().Truncate(time.Millisecond)
	nowJS := common.FormatTime(now)

	for name, tc := range map[string]struct {
		in      string
		expDate string
	}{
		"now": {
			in:      nowJS,
			expDate: now.String(),
		},
	} {
		t.Run(name, func(t *testing.T) {
			var hlc daos.HLC

			err := hlc.UnmarshalJSON([]byte(tc.in))
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			test.AssertEqual(t, tc.expDate, hlc.String(), "not equal")

			b, err := hlc.MarshalJSON()
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			test.AssertEqual(t, `"`+tc.in+`"`, string(b), "not equal")
		})
	}
}
