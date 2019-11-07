//
// (C) Copyright 2019 Intel Corporation.
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
package system

import (
	"errors"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
)

func TestScanMountInfo(t *testing.T) {
	for name, tc := range map[string]struct {
		input      string
		target     string
		scanField  int
		expMounted bool
		expError   error
	}{
		"directory mounted": {
			input:      "45 39 8:1 / /boot rw,relatime shared:28 - xfs /dev/sda1 rw,seclabel,attr2,inode64,noquota",
			target:     "/boot",
			scanField:  miMountPoint,
			expMounted: true,
		},
		"directory not mounted": {
			input:      "45 39 8:1 / /boot rw,relatime shared:28 - xfs /dev/sda1 rw,seclabel,attr2,inode64,noquota",
			target:     "/moo",
			scanField:  miMountPoint,
			expMounted: false,
		},
		"device mounted": {
			input:      "45 39 8:1 / /boot rw,relatime shared:28 - xfs /dev/sda1 rw,seclabel,attr2,inode64,noquota",
			target:     "8:1",
			scanField:  miMajorMinor,
			expMounted: true,
		},
		"device not mounted": {
			input:      "45 39 8:1 / /boot rw,relatime shared:28 - xfs /dev/sda1 rw,seclabel,attr2,inode64,noquota",
			target:     "9:1",
			scanField:  miMajorMinor,
			expMounted: false,
		},
		"weird input": {
			input:      "45 39 8:1",
			target:     "9:1",
			scanField:  miMajorMinor,
			expMounted: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			rdr := strings.NewReader(tc.input)

			gotMounted, gotErr := scanMountInfo(rdr, tc.target, tc.scanField)
			if gotErr != tc.expError {
				t.Fatalf("unexpected error (want %s, got %s)", tc.expError, gotErr)
			}
			if diff := cmp.Diff(tc.expMounted, gotMounted); diff != "" {
				t.Fatalf("unexpected mount status (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestIsMounted(t *testing.T) {
	provider := LinuxProvider{}

	for name, tc := range map[string]struct {
		target     string
		expMounted bool
		expErr     error
	}{
		"/ is mounted": {
			target:     "/",
			expMounted: true,
		},
		"/root exists but isn't mounted": {
			target:     "/root",
			expMounted: false,
		},
		"unmounted device": {
			target:     "/dev/null",
			expMounted: false,
		},
		"empty target": {
			expErr: errors.New("no such file or directory"),
		},
		"nonexistent directory": {
			target: "/fooooooooooooooooo",
			expErr: errors.New("no such file or directory"),
		},
		"neither dir nor device": {
			target: "/dev/log",
			expErr: errors.New("not a valid mount target"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotMounted, gotErr := provider.IsMounted(tc.target)

			common.CmpErr(t, tc.expErr, gotErr)
			if gotMounted != tc.expMounted {
				t.Fatalf("expected %q mounted result to be %t, got %t",
					tc.target, tc.expMounted, gotMounted)
			}
		})
	}
}
