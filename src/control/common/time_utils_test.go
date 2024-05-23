//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common_test

import (
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/test"
)

func Test_Common_ParseTime(t *testing.T) {
	zone := func(hours int) *time.Location {
		return time.FixedZone("", int((time.Duration(hours) * time.Hour).Seconds()))
	}

	for name, tc := range map[string]struct {
		in      string
		expTime time.Time
		expErr  error
	}{
		"iso861 (weird offset)": {
			in: "2021-06-03T14:29:19.461+01:30",
			expTime: time.Date(2021, 6, 3, 14, 29, 19, int(461*time.Millisecond),
				time.FixedZone("", int((90*time.Minute).Seconds()))),
		},
		"iso8601 (negative offset)": {
			in:      "2021-06-03T14:29:19.461-10:00",
			expTime: time.Date(2021, 6, 3, 14, 29, 19, int(461*time.Millisecond), zone(-10)),
		},
		"iso8601 (UTC)": {
			in:      "2021-06-03T14:29:19.461+00:00",
			expTime: time.Date(2021, 6, 3, 14, 29, 19, int(461*time.Millisecond), zone(0)),
		},
		"iso8601 (positive offset)": {
			in:      "2021-06-03T14:29:19.461+01:00",
			expTime: time.Date(2021, 6, 3, 14, 29, 19, int(461*time.Millisecond), zone(1)),
		},
		"iso8601 (strftime offset)": {
			in:      "2021-06-03T13:13:55.040-0400",
			expTime: time.Date(2021, 6, 3, 13, 13, 55, int(40*time.Millisecond), zone(-4)),
		},
		"iso861 (weird strftime offset)": {
			in: "2021-06-03T14:29:19.461+0430",
			expTime: time.Date(2021, 6, 3, 14, 29, 19, int(461*time.Millisecond),
				time.FixedZone("", int((270*time.Minute).Seconds()))),
		},
		"rfc3339 (UTC)": {
			in:      "2021-06-03T14:29:19.461Z",
			expTime: time.Date(2021, 6, 3, 14, 29, 19, int(461*time.Millisecond), zone(0)),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotTime, gotErr := common.ParseTime(tc.in)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
			if !tc.expTime.Equal(gotTime) {
				t.Fatalf("times are not equal:\n%s\n%s)", gotTime, tc.expTime)
			}
		})
	}
}

func Test_Common_FormatTime(t *testing.T) {
	zone := func(hours int) *time.Location {
		return time.FixedZone("", int((time.Duration(hours) * time.Hour).Seconds()))
	}

	for name, tc := range map[string]struct {
		in     time.Time
		expStr string
	}{
		"weird offset": {
			in: time.Date(2021, 6, 3, 14, 29, 19, int(461*time.Millisecond),
				time.FixedZone("", int((90*time.Minute).Seconds()))),
			expStr: "2021-06-03T14:29:19.461+01:30",
		},
		"negative offset": {
			in:     time.Date(2021, 6, 3, 14, 29, 19, int(461*time.Millisecond), zone(-10)),
			expStr: "2021-06-03T14:29:19.461-10:00",
		},
		"UTC": {
			in:     time.Date(2021, 6, 3, 14, 29, 19, int(461*time.Millisecond), zone(0)),
			expStr: "2021-06-03T14:29:19.461+00:00",
		},
		"positive offset": {
			in:     time.Date(2021, 6, 3, 14, 29, 19, int(46*time.Millisecond), zone(1)),
			expStr: "2021-06-03T14:29:19.046+01:00",
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotStr := common.FormatTime(tc.in)
			if diff := cmp.Diff(tc.expStr, gotStr); diff != "" {
				t.Fatalf("unexpected timestamp (-want, +got):\n%s\n", diff)
			}
		})
	}

}

func Test_Common_ParseFormattedTime(t *testing.T) {
	// Just a quick sanity check to verify that we're producing
	// timestamps that we can parse.

	formatted := common.FormatTime(time.Now())
	parsed, err := common.ParseTime(formatted)
	if err != nil {
		t.Fatalf("unable to parse formatted time %q", formatted)
	}
	if common.FormatTime(parsed) != formatted {
		t.Fatalf("after parsing, %q != %q", common.FormatTime(parsed), formatted)
	}
}
