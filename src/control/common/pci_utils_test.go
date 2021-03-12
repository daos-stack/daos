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
