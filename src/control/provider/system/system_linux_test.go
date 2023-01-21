//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"errors"
	"strings"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/google/go-cmp/cmp"
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

			test.CmpErr(t, tc.expErr, gotErr)
			if gotMounted != tc.expMounted {
				t.Fatalf("expected %q mounted result to be %t, got %t",
					tc.target, tc.expMounted, gotMounted)
			}
		})
	}
}

func TestParseFsType(t *testing.T) {
	for name, tc := range map[string]struct {
		input     string
		expFsType string
		expError  error
	}{
		"not formatted": {
			input:     "/dev/pmem1: data\n",
			expFsType: FsTypeNone,
		},
		"formatted": {
			input:     "/dev/pmem0: Linux rev 1.0 ext4 filesystem data, UUID=09619a0d-0c9e-46b4-add5-faf575dd293d\n",
			expFsType: FsTypeExt4,
		},
		"empty input": {
			expFsType: FsTypeUnknown,
		},
		"mangled short": {
			input:     "/dev/pmem0",
			expFsType: FsTypeUnknown,
		},
		"mangled medium": {
			input:     "/dev/pmem0: Linux",
			expFsType: FsTypeUnknown,
		},
		"mangled long": {
			input:     "/dev/pmem0: Linux quack bark",
			expFsType: FsTypeUnknown,
		},
		"formatted; ext2": {
			input:     "/dev/pmem0: Linux rev 1.0 ext2 filesystem data, UUID=0ce47201-6f25-4569-9e82-34c9d91173bb (large files)\n",
			expFsType: "ext2",
		},
		"garbage in header": {
			input:     "/dev/pmem1: COM executable for DOS",
			expFsType: "DOS",
		},
	} {
		t.Run(name, func(t *testing.T) {
			if diff := cmp.Diff(tc.expFsType, parseFsType(tc.input)); diff != "" {
				t.Fatalf("unexpected fsType (-want, +got):\n%s\n", diff)
			}
		})
	}
}
