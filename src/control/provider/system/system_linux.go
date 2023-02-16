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
	FsTypeTmpfs   = "tmpfs"
	FsTypeUnknown = "unknown"

	parseFsUnformatted = "data"
)

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

func getDistroArgs() []string {
	distro := GetDistribution()

	// use stride option for SCM interleaved mode
	// disable lazy initialization (hurts perf)
	// discard is not needed/supported on SCM
	opts := "stride=512,lazy_itable_init=0,lazy_journal_init=0,nodiscard"

	// enable flex_bg to allow larger contiguous block allocation
	// disable uninit_bg to initialize everything upfront
	// disable resize to avoid GDT block allocations
	// disable extra isize since we really have no use for this
	feat := "flex_bg,^uninit_bg,^resize_inode,^extra_isize"

	switch {
	case distro.ID == "centos" && distro.Version.Major < 8:
		// use strict minimum listed above here since that's
		// the oldest distribution we support
	default:
		// packed_meta_blocks allows to group all data blocks together
		opts += ",packed_meta_blocks=1"
		// enable sparse_super2 since 2x superblock copies are enough
		// disable csum since we have ECC already for SCM
		// bigalloc is intentionally not used since some kernels don't support it
		feat += ",sparse_super2,^metadata_csum"
	}

	return []string{
		"-E", opts,
		"-O", feat,
		// each ext4 group is of size 32767 x 4KB = 128M
		// pack 128 groups together to increase ability to use huge
		// pages for a total virtual group size of 16G
		"-G", "128",
	}
}

// Mkfs attempts to create a filesystem of the supplied type, on the
// supplied device.
func (s LinuxProvider) Mkfs(fsType, device string, force bool) error {
	cmdPath, err := exec.LookPath(fmt.Sprintf("mkfs.%s", fsType))
	if err != nil {
		return errors.Wrapf(err, "unable to find mkfs.%s", fsType)
	}

	if err := s.checkDevice(device); err != nil {
		return err
	}

	// TODO: Think about a way to allow for some kind of progress
	// callback so that the user has some visibility into long-running
	// format operations (very large devices).
	args := []string{
		// use quiet mode
		"-q",
		// use direct i/o to avoid polluting page cache
		"-D",
		// use DAOS label
		"-L", "daos",
		// don't reserve blocks for super-user
		"-m", "0",
		// use largest possible block size
		"-b", "4096",
		// don't need large inode, 128B is enough
		// since we don't use xattr
		"-I", "128",
		// reduce the inode per bytes ratio
		// one inode for 64M is more than enough
		"-i", "67108864",
	}
	args = append(args, getDistroArgs()...)
	// string always comes last
	args = append(args, []string{device}...)
	if force {
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
