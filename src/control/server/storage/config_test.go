//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"testing"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/pkg/errors"
)

func TestStorage_parsePCIBusRange(t *testing.T) {
	for name, tc := range map[string]struct {
		rangeStr string
		bitSize  int
		expBegin uint8
		expEnd   uint8
		expErr   error
	}{
		"hexadecimal": {
			rangeStr: "0x80-0x8f",
			expBegin: 0x80,
			expEnd:   0x8f,
		},
		"incorrect hexadecimal": {
			rangeStr: "0x8g-0x8f",
			expErr:   errors.New("parsing \"0x8g\""),
		},
		"hexadecimal upper": {
			rangeStr: "0x80-0x8F",
			expBegin: 0x80,
			expEnd:   0x8F,
		},
		"decimal": {
			rangeStr: "128-143",
			expBegin: 0x80,
			expEnd:   0x8F,
		},
		"bad range": {
			rangeStr: "128-143-0",
			expErr:   errors.New("invalid busid range \"128-143-0\""),
		},
		"reverse range": {
			rangeStr: "143-0",
			expErr:   errors.New("invalid busid range \"143-0\""),
		},
		"bad separator": {
			rangeStr: "143:0",
			expErr:   errors.New("invalid busid range \"143:0\""),
		},
		"hexadecimal; no prefix": {
			rangeStr: "00-5d",
			bitSize:  8,
			expErr:   errors.New("invalid syntax"),
		},
		"hexadecimal; bit-size exceeded": {
			rangeStr: "0x000-0x5dd",
			bitSize:  8,
			expErr:   errors.New("value out of range"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			begin, end, err := parsePCIBusRange(tc.rangeStr, tc.bitSize)
			common.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			common.AssertEqual(t, tc.expBegin, begin, "bad beginning limit")
			common.AssertEqual(t, tc.expEnd, end, "bad ending limit")
		})
	}
}
