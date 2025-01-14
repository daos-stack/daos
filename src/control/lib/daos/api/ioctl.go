//
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build !test_stubs
// +build !test_stubs

package api

/*
#include <fcntl.h>
#include <sys/ioctl.h>

#include <dfuse_ioctl.h>

static int
call_dfuse_telemetry_ioctl(char *path, bool enabled)
{
	int fd;
	int rc;

	fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	if (fd < 0)
		return errno;

	errno = 0;
	rc = ioctl(fd, DFUSE_IOCTL_METRICS_TOGGLE, &enabled);
	if (rc != 0) {
		int err = errno;

		close(fd);
		return err;
	}
	close(fd);

	return 0;
}
*/
import "C"

func call_dfuse_telemetry_ioctl(path string, enabled bool) error {
	cPath := C.CString(path)
	defer freeString(cPath)

	_, err := C.call_dfuse_telemetry_ioctl(cPath, C.bool(enabled))
	if err != nil {
		return err
	}

	return nil
}
