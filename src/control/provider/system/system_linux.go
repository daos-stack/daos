//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"syscall"

	"github.com/pkg/errors"
	"golang.org/x/sys/unix"
)

const (
	FsTypeNone    = "none"
	FsTypeExt4    = "ext4"
	FsTypeBtrfs   = "btrfs"
	FsTypeXfs     = "xfs"
	FsTypeZfs     = "zfs"
	FsTypeNtfs    = "ntfs"
	FsTypeTmpfs   = "tmpfs"
	FsTypeNfs     = "nfs"
	FsTypeUnknown = "unknown"

	parseFsUnformatted = "data"

	// magic numbers harvested from statfs man page
	MagicTmpfs = 0x01021994
	MagicExt4  = 0xEF53
	MagicBtrfs = 0x9123683E
	MagicXfs   = 0x58465342
	MagicZfs   = 0x2FC12FC1
	MagicNtfs  = 0x5346544e
	MagicNfs   = 0x6969
)

var magicToStr = map[int64]string{
	MagicBtrfs: FsTypeBtrfs,
	MagicExt4:  FsTypeExt4,
	MagicNfs:   FsTypeNfs,
	MagicNtfs:  FsTypeNtfs,
	MagicTmpfs: FsTypeTmpfs,
	MagicXfs:   FsTypeXfs,
	MagicZfs:   FsTypeZfs,
}

// DefaultProvider returns the package-default provider implementation.
func DefaultProvider() *LinuxProvider {
	return &LinuxProvider{}
}

// LinuxProvider encapsulates Linux-specific implementations of system
// interfaces.
type LinuxProvider struct{}

// mountId,parentId,major:minor,root,mountPoint
const (
	_ int = iota
	_
	miMajorMinor
	_
	miMountPoint
	miNumFields
)

func scanMountInfo(input io.Reader, target string, scanField int) (bool, error) {
	scn := bufio.NewScanner(input)
	for scn.Scan() {
		fields := strings.Fields(scn.Text())
		if len(fields) < miNumFields {
			continue
		}
		if fields[scanField] == target {
			return true, nil
		}
	}

	return false, scn.Err()
}

func resolveDeviceMount(device string) (string, error) {
	fi, err := os.Stat(device)
	if err != nil {
		return "", err
	}

	majorMinor, err := resolveDevice(fi)
	if err != nil {
		return "", err
	}

	mi, err := os.Open("/proc/self/mountinfo")
	if err != nil {
		return "", err
	}
	defer mi.Close()

	scn := bufio.NewScanner(mi)
	for scn.Scan() {
		fields := strings.Fields(scn.Text())
		if len(fields) < miNumFields {
			continue
		}
		if fields[miMajorMinor] == majorMinor {
			return fields[miMountPoint], nil
		}
	}

	return "", errors.Errorf("Unable to resolve %s to mountpoint", device)
}

func resolveDevice(fi os.FileInfo) (string, error) {
	if fi.Mode()&os.ModeDevice == 0 {
		return "", errors.Errorf("%s is not a device", fi.Name())
	}
	st, ok := fi.Sys().(*syscall.Stat_t)
	if !ok {
		return "", errors.Errorf("unable to get underlying stat for %v", fi)
	}
	return fmt.Sprintf("%d:%d", st.Rdev/256, st.Rdev%256), nil
}

func isDevice(target string) bool {
	fi, err := os.Stat(target)
	if err != nil {
		return false
	}

	return fi.Mode()&os.ModeDevice != 0
}

// IsMounted checks the target device or directory for mountedness.
func (s LinuxProvider) IsMounted(target string) (bool, error) {
	fi, err := os.Stat(target)
	if err != nil {
		return false, err
	}

	var scanField int
	switch {
	case fi.IsDir():
		target = filepath.Clean(target)
		scanField = miMountPoint
	case fi.Mode()&os.ModeDevice != 0:
		scanField = miMajorMinor
		target, err = resolveDevice(fi)
		if err != nil {
			return false, err
		}
	default:
		return false, errors.Errorf("not a valid mount target: %q", target)
	}

	mi, err := os.Open("/proc/self/mountinfo")
	if err != nil {
		return false, err
	}
	defer mi.Close()

	return scanMountInfo(mi, target, scanField)
}

// Mount provides an implementation of Mounter which calls the system implementation.
func (s LinuxProvider) Mount(source, target, fstype string, flags uintptr, data string) error {
	return unix.Mount(source, target, fstype, flags, data)
}

// Unmount provides an implementation of Unmounter which calls the system implementation.
func (s LinuxProvider) Unmount(target string, flags int) error {
	// umount(2) in Linux doesn't allow unmount of a block device
	if isDevice(target) {
		mntPoint, err := resolveDeviceMount(target)
		if err != nil {
			return err
		}
		target = mntPoint
	}
	return unix.Unmount(target, flags)
}

// GetfsUsage retrieves total and available byte counts for a mountpoint.
func (s LinuxProvider) GetfsUsage(target string) (uint64, uint64, error) {
	stBuf := new(unix.Statfs_t)

	if err := unix.Statfs(target, stBuf); err != nil {
		return 0, 0, err
	}

	frSize := uint64(stBuf.Frsize)

	return frSize * stBuf.Blocks, frSize * stBuf.Bavail, nil
}

type FsType struct {
	Name   string
	NoSUID bool
}

// GetFsType retrieves the filesystem type for a path.
func (s LinuxProvider) GetFsType(path string) (*FsType, error) {
	stBuf := new(unix.Statfs_t)

	if err := unix.Statfs(path, stBuf); err != nil {
		return nil, err
	}

	fsType := &FsType{
		Name:   fsStrFromMagic(stBuf.Type),
		NoSUID: (stBuf.Flags&unix.ST_NOSUID != 0),
	}

	return fsType, nil
}

func fsStrFromMagic(typeMagic int64) string {
	str, found := magicToStr[typeMagic]
	if !found {
		return FsTypeUnknown
	}
	return str
}

func (s LinuxProvider) checkDevice(device string) error {
	st, err := s.Stat(device)
	if err != nil {
		return errors.Wrapf(err, "stat failed on %s", device)
	}

	if st.Mode()&os.ModeDevice == 0 {
		return errors.Errorf("%s is not a device file", device)
	}

	return nil
}

// MkfsReq defines the input parameters for a Mkfs call.
type MkfsReq struct {
	Device     string
	Filesystem string
	Options    []string
	Force      bool
}

// Mkfs attempts to create a filesystem of the supplied type, on the
// supplied device.
func (s LinuxProvider) Mkfs(req MkfsReq) error {
	cmdPath, err := exec.LookPath(fmt.Sprintf("mkfs.%s", req.Filesystem))
	if err != nil {
		return errors.Wrapf(err, "unable to find mkfs.%s", req.Filesystem)
	}

	if err := s.checkDevice(req.Device); err != nil {
		return err
	}

	args := make([]string, 0, len(req.Options))
	_ = copy(args, req.Options)
	// TODO: Think about a way to allow for some kind of progress
	// callback so that the user has some visibility into long-running
	// format operations (very large devices).

	// string always comes last
	args = append(args, []string{req.Device}...)
	if req.Force {
		args = append([]string{"-F"}, args...)
	}
	out, err := exec.Command(cmdPath, args...).Output()
	if err != nil {
		return &RunCmdError{
			Wrapped: err,
			Stdout:  string(out),
		}
	}

	return nil
}

// Getfs probes the specified device in an attempt to determine the
// formatted filesystem type, if any.
func (s LinuxProvider) Getfs(device string) (string, error) {
	cmdPath, err := exec.LookPath("file")
	if err != nil {
		return FsTypeNone, errors.Wrap(err, "unable to find file")
	}

	if err := s.checkDevice(device); err != nil {
		return FsTypeNone, err
	}

	args := []string{"-s", device}
	out, err := exec.Command(cmdPath, args...).Output()
	if err != nil {
		return FsTypeNone, &RunCmdError{
			Wrapped: err,
			Stdout:  string(out),
		}
	}

	return parseFsType(string(out)), nil
}

// Stat probes the specified path and returns os level file info.
func (s LinuxProvider) Stat(path string) (os.FileInfo, error) {
	return os.Stat(path)
}

// Chmod changes the mode of the specified path.
func (s LinuxProvider) Chmod(path string, mode os.FileMode) error {
	return os.Chmod(path, mode)
}

// Chown changes the ownership of the specified path.
func (s LinuxProvider) Chown(path string, uid, gid int) error {
	return os.Chown(path, uid, gid)
}

func parseFsType(input string) string {
	// /dev/pmem0: Linux rev 1.0 ext4 filesystem data, UUID=09619a0d-0c9e-46b4-add5-faf575dd293d
	// /dev/pmem1: data
	// /dev/pmem1: COM executable for DOS
	fields := strings.Fields(input)
	switch {
	case len(fields) == 2 && fields[1] == parseFsUnformatted:
		return FsTypeNone
	case len(fields) >= 5:
		return fields[4]
	default:
		return FsTypeUnknown
	}
}
