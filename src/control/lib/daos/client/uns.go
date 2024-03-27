package client

import (
	"context"
	"path/filepath"
	"syscall"

	"github.com/google/uuid"
	"github.com/pkg/errors"
)

/*
#include <stdio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>

#include <daos/common.h>
#include <daos_types.h>
#include <daos_api.h>
#include <daos_cont.h>
#include <daos_array.h>
#include <daos_fs.h>
#include <daos_uns.h>
#include <dfuse_ioctl.h>

// cgo doesn't work with variadic functions, so we need to
// wrap these.
static int
dfuse_open(char *path, int oflag)
{
	return open(path, oflag);
}

static int
dfuse_ioctl(int fd, struct dfuse_il_reply *reply)
{
	return ioctl(fd, DFUSE_IOCTL_IL, reply);
}
*/
import "C"

func (b *daosClientBinding) dfuse_open(path *C.char, oflag C.int) (C.int, error) {
	// use the two-value form to get at errno
	fd, err := C.dfuse_open(path, oflag)
	return fd, err
}

func (m *mockApiClient) dfuse_open(path *C.char, oflag C.int) (C.int, error) {
	rc := m.getRc("dfuse_open", 0)
	if rc != 0 {
		return -1, syscall.Errno(rc)
	}
	return 3, nil
}

func (b *daosClientBinding) close_fd(fd C.int) C.int {
	return C.close(fd)
}

func (m *mockApiClient) close_fd(fd C.int) C.int {
	return m.getRc("close_fd", 0)
}

func (b *daosClientBinding) dfuse_ioctl(fd C.int, reply *C.struct_dfuse_il_reply) (C.int, error) {
	// use the two-value form to get at errno
	rc, err := C.dfuse_ioctl(fd, reply)
	return rc, err
}

func (m *mockApiClient) dfuse_ioctl(fd C.int, reply *C.struct_dfuse_il_reply) (C.int, error) {
	if rc := m.getRc("dfuse_ioctl", 0); rc != 0 {
		return rc, syscall.Errno(rc)
	}

	reply.fir_pool = uuidToC(m.cfg.ConnectedPool)
	reply.fir_cont = uuidToC(m.cfg.ConnectedCont)
	reply.fir_version = C.DFUSE_IOCTL_VERSION

	return 0, nil
}

func (b *daosClientBinding) duns_resolve_path(path *C.char, attr *C.struct_duns_attr_t) C.int {
	return C.duns_resolve_path(path, attr)
}

func (m *mockApiClient) duns_resolve_path(path *C.char, attr *C.struct_duns_attr_t) C.int {
	if rc := m.getRc("duns_resolve_path", 0); rc != 0 {
		return rc
	}

	poolStr := C.CString(m.cfg.ConnectedPool.String())
	defer freeString(poolStr)
	C.strcpy(&attr.da_pool[0], poolStr)

	contStr := C.CString(m.cfg.ConnectedCont.String())
	defer freeString(contStr)
	C.strcpy(&attr.da_cont[0], contStr)

	return 0
}

func callDfuseIoctl(client apiClient, path *C.char, reply *C.struct_dfuse_il_reply) error {
	fd, err := client.dfuse_open(path, C.O_RDONLY|C.O_DIRECTORY|C.O_NOFOLLOW)
	if fd < 0 {
		return err
	}
	defer client.close_fd(fd)

	rc, err := client.dfuse_ioctl(fd, reply)
	if rc != 0 {
		return err
	}

	if reply.fir_version != C.DFUSE_IOCTL_VERSION {
		return daosError(C.daos_errno2der(C.EIO))
	}

	return nil
}

// ResolveContainerPath resolves the given path to a set of pool and container IDs.
func ResolveContainerPath(ctx context.Context, path string) (poolID string, containerID string, err error) {
	var dattr C.struct_duns_attr_t
	var ilReply C.struct_dfuse_il_reply

	client, err := getApiClient(ctx)
	if err != nil {
		return
	}

	cPath := C.CString(path)
	defer freeString(cPath)

	if err = callDfuseIoctl(client, cPath, &ilReply); err == nil {
		var poolUUID uuid.UUID
		var contUUID uuid.UUID

		poolUUID, err = uuidFromC(ilReply.fir_pool)
		if err != nil {
			return
		}
		poolID = poolUUID.String()

		contUUID, err = uuidFromC(ilReply.fir_cont)
		if err != nil {
			return
		}
		containerID = contUUID.String()

		log.Debugf("resolved dfuse path %s to %s/%s", path, poolID, containerID)
		return
	} else if !errors.Is(err, syscall.ENOTTY) {
		var der C.int = -C.DER_UNKNOWN
		if errno, ok := err.(syscall.Errno); ok {
			der = C.daos_errno2der(C.int(errno))
		}
		err = errors.Wrap(daosError(der), "dfuse_ioctl() failed")
		return
	}

	if err = daosError(C.daos_errno2der(client.duns_resolve_path(cPath, &dattr))); err != nil {
		return
	}
	defer C.duns_destroy_attr(&dattr)

	poolID = C.GoString(&dattr.da_pool[0])
	containerID = C.GoString(&dattr.da_cont[0])

	log.Debugf("resolved duns path %s to %s/%s", path, poolID, containerID)

	return
}

// ResolvePoolPath resolves the given path to a pool ID.
func ResolvePoolPath(ctx context.Context, path string) (poolID string, err error) {
	var ilReply C.struct_dfuse_il_reply

	client, err := getApiClient(ctx)
	if err != nil {
		return
	}

	cDirPath := C.CString(filepath.Dir(path))
	defer freeString(cDirPath)

	if err = callDfuseIoctl(client, cDirPath, &ilReply); err != nil {
		return
	}

	var poolUUID uuid.UUID
	poolUUID, err = uuidFromC(ilReply.fir_pool)
	if err != nil {
		return
	}

	log.Debugf("resolved dfuse path %s to %s", path, poolUUID.String())

	return poolUUID.String(), nil
}
