//
// (C) Copyright 2019-2024 Intel Corporation.
// (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"errors"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"syscall"
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

	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	testFilePath := tmpDir + "/testfile"
	if err := os.WriteFile(testFilePath, []byte("test"), 0644); err != nil {
		t.Fatalf("unable to create test file %q: %v", testFilePath, err)
	}

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
			target: testFilePath,
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

func TestSystemLinux_GetfsType(t *testing.T) {
	for name, tc := range map[string]struct {
		path      string
		expResult *FsType
		expErr    error
	}{
		"no path": {
			expErr: syscall.ENOENT,
		},
		"bad path": {
			path:   "notreal",
			expErr: syscall.ENOENT,
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := DefaultProvider().GetfsType(tc.path)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("unexpected fsType (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func validDev(t *testing.T) string {
	t.Helper()

	// Only want numbered partitions, not whole disks.
	// Exclude loop/nbd devices which may not be attached.
	re := regexp.MustCompile(`^[a-zA-Z]+[0-9]+$`)
	exclude := regexp.MustCompile(`^(loop|nbd|zram)`)

	sysRoot := "/sys/class/block/"
	entries, err := os.ReadDir(sysRoot)
	if err != nil {
		t.Fatalf("unable to read %q: %v", sysRoot, err)
	}

	for _, entry := range entries {
		if !re.MatchString(entry.Name()) || exclude.MatchString(entry.Name()) {
			continue
		}

		devPath := "/dev/" + entry.Name()
		info, err := os.Stat(devPath)
		if err != nil {
			continue
		}
		if (info.Mode()&os.ModeDevice) != 0 && (info.Mode()&os.ModeCharDevice) == 0 {
			t.Logf("using block device %q for test", devPath)
			return devPath
		}
	}

	t.Fatal("no valid block device found for test")
	return ""
}

func TestSystemLinux_GetDeviceLabel(t *testing.T) {
	for name, tc := range map[string]struct {
		path   string
		expErr error
	}{
		"no path": {
			expErr: errors.New("empty path"),
		},
		"nonexistent": {
			path:   "fake",
			expErr: syscall.ENOENT,
		},
		"not a device": {
			path:   "/tmp",
			expErr: errors.New("not a device file"),
		},
		"valid block device": {
			path: validDev(t),
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := DefaultProvider().GetDeviceLabel(tc.path)

			test.CmpErr(t, tc.expErr, err)

			if tc.expErr != nil {
				test.AssertEqual(t, "", result, "")
			} else {
				// We can't predict the label since it's system dependent. It might even be empty.
				t.Logf("got label %q", result)
			}
		})
	}
}

func TestSystemLinux_fsStrFromMagic(t *testing.T) {
	for name, tc := range map[string]struct {
		magic     int64
		expResult string
	}{
		"ext4": {
			magic:     MagicExt4,
			expResult: FsTypeExt4,
		},
		"tmpfs": {
			magic:     MagicTmpfs,
			expResult: FsTypeTmpfs,
		},
		"nfs": {
			magic:     MagicNfs,
			expResult: FsTypeNfs,
		},
		"ntfs": {
			magic:     MagicNtfs,
			expResult: FsTypeNtfs,
		},
		"btrfs": {
			magic:     MagicBtrfs,
			expResult: FsTypeBtrfs,
		},
		"xfs": {
			magic:     MagicXfs,
			expResult: FsTypeXfs,
		},
		"zfs": {
			magic:     MagicZfs,
			expResult: FsTypeZfs,
		},
		"unknown": {
			magic:     0x1,
			expResult: FsTypeUnknown,
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expResult, fsStrFromMagic(tc.magic), "")
		})
	}
}

func TestSystemLinux_Mkfs(t *testing.T) {
	for name, tc := range map[string]struct {
		req        MkfsReq
		expErr     error
		expCmdName string
		expCmdArgs []string
	}{
		"empty": {
			req:    MkfsReq{},
			expErr: errors.New("no filesystem"),
		},
		"bad filesystem": {
			req: MkfsReq{
				Filesystem: "moo",
			},
			expErr: errors.New("unable to find mkfs.moo"),
		},
		"bad device": {
			req: MkfsReq{
				Filesystem: "ext4",
				Device:     "/notreal",
			},
			expErr: syscall.ENOENT,
		},
		"success": {
			req: MkfsReq{
				Filesystem: "ext4",
				Device:     validDev(t), // real device, but actual mkfs command is mocked
			},
			expCmdName: "mkfs.ext4",
			expCmdArgs: []string{validDev(t)},
		},
		"force": {
			req: MkfsReq{
				Filesystem: "ext4",
				Device:     validDev(t),
				Force:      true,
			},
			expCmdName: "mkfs.ext4",
			expCmdArgs: []string{"-F", validDev(t)},
		},
		"options": {
			req: MkfsReq{
				Filesystem: "ext4",
				Device:     validDev(t),
				Options:    []string{"-L", "my_device"},
			},
			expCmdName: "mkfs.ext4",
			expCmdArgs: []string{"-L", "my_device", validDev(t)},
		},
	} {
		t.Run(name, func(t *testing.T) {
			p := DefaultProvider()

			var seenName string
			var seenArgs []string
			p.runCommand = func(name string, args ...string) ([]byte, error) {
				seenName = name
				seenArgs = args
				return []byte{}, nil
			}

			err := p.Mkfs(tc.req)

			test.CmpErr(t, tc.expErr, err)

			if seenName != "" {
				// don't care where the binary was found, just that it was
				seenName = filepath.Base(seenName)
			}
			test.AssertEqual(t, tc.expCmdName, seenName, "mkfs command name")
			test.AssertEqual(t, tc.expCmdArgs, seenArgs, "mkfs args")
		})
	}
}
