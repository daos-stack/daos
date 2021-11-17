//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"testing"

	"github.com/pkg/errors"
)

func TestCommon_ParsePCIAddress(t *testing.T) {
	for name, tc := range map[string]struct {
		addrStr string
		expDom  uint64
		expBus  uint64
		expDev  uint64
		expFun  uint64
		expErr  error
	}{
		"valid": {
			addrStr: "0000:80:00.0",
			expBus:  0x80,
		},
		"invalid": {
			addrStr: "0000:gg:00.0",
			expErr:  errors.New("parsing \"gg\""),
		},
		"vmd address": {
			addrStr: "0000:5d:05.5",
			expBus:  0x5d,
			expDev:  0x05,
			expFun:  0x05,
		},
		"vmd backing device address": {
			addrStr: "5d0505:01:00.0",
			expDom:  0x5d0505,
			expBus:  0x01,
		},
	} {
		t.Run(name, func(t *testing.T) {
			dom, bus, dev, fun, err := ParsePCIAddress(tc.addrStr)
			CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			AssertEqual(t, tc.expDom, dom, "bad domain")
			AssertEqual(t, tc.expBus, bus, "bad bus")
			AssertEqual(t, tc.expDev, dev, "bad device")
			AssertEqual(t, tc.expFun, fun, "bad func")
		})
	}
}

func TestCommon_GetRangeLimits(t *testing.T) {
	for name, tc := range map[string]struct {
		rangeStr string
		expBegin uint64
		expEnd   uint64
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
	} {
		t.Run(name, func(t *testing.T) {
			begin, end, err := GetRangeLimits(tc.rangeStr)
			CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			AssertEqual(t, tc.expBegin, begin, "bad beginning limit")
			AssertEqual(t, tc.expEnd, end, "bad ending limit")
		})
	}
}
