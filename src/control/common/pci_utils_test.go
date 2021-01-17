//
// (C) Copyright 2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
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
