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

package spdk

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestSpdk_revertBackingToVmd(t *testing.T) {
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
		"invalid pci address": {
			inAddrs: []string{"0000:gg:00.0"},
			expErr:  errors.New("parsing \"gg\""),
		},
		"invalid vmd domain in address": {
			inAddrs: []string{"5d055:01:00.0"},
			expErr:  errors.New("unexpected length of domain"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			gotAddrs, gotErr := revertBackingToVmd(log, tc.inAddrs)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expOutAddrs, gotAddrs); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}
