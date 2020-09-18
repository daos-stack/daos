//
// (C) Copyright 2018-2020 Intel Corporation.
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

type ext struct {
	removeCalls []string
	removeErr   error
}

var (
	mockExt    = ext{}
	sampleErr1 = errors.New("example error #1")
	sampleErr2 = errors.New("example error #2")
)

func mockRemove(name string) error {
	mockExt.removeCalls = append(mockExt.removeCalls, name)
	return mockExt.removeErr
}

func TestSpdk_CleanLockfiles(t *testing.T) {
	for name, tc := range map[string]struct {
		pciAddrs  []string
		removeErr error
		expCalls  []string
		expErr    error
	}{
		"no pciAddrs": {},
		"single pciAddr": {
			pciAddrs: []string{"0000:81:00.0"},
		},
		"multiple pciAddrs": {
			pciAddrs: []string{"0000:81:00.0", "0000:82:00.0"},
		},
		"error on remove": {
			pciAddrs:  []string{"0000:81:00.0", "0000:82:00.0"},
			removeErr: sampleErr1,
			expCalls:  []string{lockfilePathPrefix + "0000:81:00.0"},
			expErr:    sampleErr1,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.expCalls == nil {
				tc.expCalls = make([]string, 0, len(tc.pciAddrs))
				for _, p := range tc.pciAddrs {
					tc.expCalls = append(tc.expCalls,
						lockfilePathPrefix+p)
				}
			}

			mockExt.removeCalls = make([]string, 0, len(tc.pciAddrs))
			mockExt.removeErr = tc.removeErr

			gotErr := cleanLockfiles(log, mockRemove, tc.pciAddrs...)
			common.CmpErr(t, tc.expErr, gotErr)

			if diff := cmp.Diff(tc.expCalls, mockExt.removeCalls); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}

func TestSpdk_WrapCleanError(t *testing.T) {
	wrappedErr := errors.Wrap(sampleErr1, sampleErr2.Error())

	for name, tc := range map[string]struct {
		inErr     error
		cleanErr  error
		expOutErr error
	}{
		"no errors":              {nil, nil, nil},
		"clean error":            {nil, sampleErr1, sampleErr1},
		"outer error":            {sampleErr1, nil, sampleErr1},
		"outer and clean errors": {sampleErr1, sampleErr2, wrappedErr},
	} {
		t.Run(name, func(t *testing.T) {
			gotErr := wrapCleanError(tc.inErr, tc.cleanErr)
			common.CmpErr(t, tc.expOutErr, gotErr)
		})
	}
}
