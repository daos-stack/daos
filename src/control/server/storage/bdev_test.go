//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"fmt"
	"testing"

	"github.com/daos-stack/daos/src/control/common"
)

func Test_NvmeDevState(t *testing.T) {
	for name, tc := range map[string]struct {
		state       NvmeDevState
		expIsNew    bool
		expIsNormal bool
		expIsFaulty bool
		expStr      string
	}{
		"new state": {
			state:    NvmeStatePlugged,
			expIsNew: true,
			expStr:   "NEW",
		},
		"normal state": {
			state:       NvmeStatePlugged | NvmeStateInUse,
			expIsNormal: true,
			expStr:      "NORMAL",
		},
		"faulty state": {
			state:       NvmeStatePlugged | NvmeStateInUse | NvmeStateFaulty,
			expIsFaulty: true,
			expStr:      "EVICTED",
		},
		"new and faulty state": {
			state:       NvmeStatePlugged | NvmeStateFaulty,
			expIsNew:    true,
			expIsFaulty: true,
			expStr:      "EVICTED",
		},
		"unplugged, new and faulty": {
			state:  NvmeStateFaulty,
			expStr: "UNPLUGGED",
		},
		"new and identify state": {
			state:    NvmeStatePlugged | NvmeStateIdentify,
			expIsNew: true,
			expStr:   "NEW|IDENTIFY",
		},
		"new, faulty and identify state": {
			state:       NvmeStatePlugged | NvmeStateFaulty | NvmeStateIdentify,
			expIsNew:    true,
			expIsFaulty: true,
			expStr:      "EVICTED|IDENTIFY",
		},
		"normal and identify state": {
			state:       NvmeStatePlugged | NvmeStateInUse | NvmeStateIdentify,
			expIsNormal: true,
			expStr:      "NORMAL|IDENTIFY",
		},
	} {
		t.Run(name, func(t *testing.T) {
			common.AssertEqual(t, tc.expIsNew, tc.state.IsNew(),
				"unexpected IsNew() result")
			common.AssertEqual(t, tc.expIsNormal, tc.state.IsNormal(),
				"unexpected IsNormal() result")
			common.AssertEqual(t, tc.expIsFaulty, tc.state.IsFaulty(),
				"unexpected IsFaulty() result")
			common.AssertEqual(t, tc.expStr, tc.state.StatusString(),
				"unexpected status string")

			stateNew, err := NvmeDevStateFromString(tc.state.StatusString())
			if err != nil {
				t.Fatal(err)
			}

			common.AssertEqual(t, tc.state.StatusString(), stateNew.StatusString(),
				fmt.Sprintf("expected string %s to yield state %s",
					tc.state.StatusString(), stateNew.StatusString()))
		})
	}
}

func Test_NvmeDevStateFromString_invalid(t *testing.T) {
	if _, err := NvmeDevStateFromString("BAR"); err == nil {
		t.Fatal("should fail")
	}
}
