//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package spdk

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestSpdk_backingAddress2VMD(t *testing.T) {
	for name, tc := range map[string]struct {
		inAddrs     []string
		expOutAddrs []string
		expErr      error
	}{
		"empty": {},
		"no vmd addresses": {
			inAddrs:     []string{"0000:80:00.0"},
			expOutAddrs: []string{"0000:80:00.0"},
		},
		"single vmd address": {
			inAddrs:     []string{"5d0505:01:00.0"},
			expOutAddrs: []string{"0000:5d:05.5"},
		},
		"multiple vmd address": {
			inAddrs:     []string{"5d0505:01:00.0", "5d0505:03:00.0"},
			expOutAddrs: []string{"0000:5d:05.5"},
		},
		"invalid vmd domain in address": {
			inAddrs: []string{"5d055:01:00.0"},
			expErr:  errors.New("unexpected length of vmd domain"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			addrList, err := common.NewPCIAddressList(tc.inAddrs...)
			if err != nil {
				t.Fatal(err)
			}

			gotAddrs, gotErr := backingAddress2VMD(log, addrList)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expOutAddrs, gotAddrs.Strings()); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}
