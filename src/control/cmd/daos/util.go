//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"os"
	"unsafe"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
)

/*
// NB: There should only be one set of CFLAGS/LDFLAGS definitions
// for the whole package!
#cgo CFLAGS: -I${SRCDIR}/../../../utils
#cgo LDFLAGS: -lgurt -lcart -ldaos -ldaos_common -lduns -ldfs -luuid -ldaos_cmd_hdlrs

#include "util.h"

void
init_op_vals(struct cmd_args_s *ap)
{
	// Annoyingly, cgo insists that these fields are
	// uint32_t, and refuses to allow assignment of -1
	// from the Go side, so we do this here.
	ap->p_op = -1;
	ap->c_op = -1;
	ap->o_op = -1;
	ap->fs_op = -1;
	ap->sh_op = -1;
}

void
free_daos_alloc(void *ptr)
{
	// Use the macro to free memory allocated
	// by DAOS macros in order to keep NLT happy.
	D_FREE(ptr);
}
*/
import "C"

func apiVersion() string {
	return fmt.Sprintf("%d.%d.%d",
		C.DAOS_API_VERSION_MAJOR,
		C.DAOS_API_VERSION_MINOR,
		C.DAOS_API_VERSION_FIX,
	)
}

func daosError(rc C.int) error {
	if rc == 0 {
		return nil
	}
	return daos.Status(rc)
}

func goBool2int(in bool) (out C.int) {
	if in {
		out = 1
	}
	return
}

func copyUUID(dst *C.uuid_t, src uuid.UUID) error {
	if dst == nil {
		return errors.New("nil dest uuid_t")
	}

	for i, v := range src {
		dst[i] = C.uchar(v)
	}

	return nil
}

func uuidToC(in uuid.UUID) (out C.uuid_t) {
	for i, v := range in {
		out[i] = C.uchar(v)
	}

	return
}

func uuidFromC(cUUID C.uuid_t) (uuid.UUID, error) {
	return uuid.FromBytes(C.GoBytes(unsafe.Pointer(&cUUID[0]), C.int(len(cUUID))))
}

func iterStringsBuf(cBuf unsafe.Pointer, expected C.size_t, cb func(string)) error {
	var curLen C.size_t

	// Create a Go slice for easy iteration (no pointer arithmetic in Go).
	bufSlice := (*[1 << 30]C.char)(cBuf)[:expected:expected]
	for total := C.size_t(0); total < expected; total += curLen + 1 {
		chunk := bufSlice[total:]
		curLen = C.strnlen(&chunk[0], expected-total)

		if curLen >= expected-total {
			return errors.New("corrupt buffer")
		}

		chunk = bufSlice[total : total+curLen]
		cb(C.GoString(&chunk[0]))
	}

	return nil
}

func fd2FILE(fd uintptr, modeStr string) (out *C.FILE, err error) {
	cModeStr := C.CString(modeStr)
	defer freeString(cModeStr)
	out = C.fdopen(C.int(fd), cModeStr)
	if out == nil {
		return nil, errors.Errorf("fdopen() failed (fd: %d, mode: %s)", fd, modeStr)
	}
	return
}

func freeString(str *C.char) {
	if str == nil {
		return
	}
	C.free(unsafe.Pointer(str))
}

func createWriteStream(ctx context.Context, printLn func(line string)) (*C.FILE, func(), error) {
	// Create a FILE object for the handler to use for printing output or errors, and call the
	// callback for each line.
	r, w, err := os.Pipe()
	if err != nil {
		return nil, nil, err
	}
	done := make(chan bool, 1)

	stream, err := fd2FILE(w.Fd(), "w")
	if err != nil {
		return nil, nil, err
	}

	go func(ctx context.Context) {
		defer close(done)

		rdr := bufio.NewReader(r)
		for {
			select {
			case <-ctx.Done():
				return
			default:
				line, err := rdr.ReadString('\n')
				if err != nil {
					if !errors.Is(err, io.EOF) {
						printLn(fmt.Sprintf("read err: %s", err))
					}
					return
				}
				printLn(line)
			}
		}
	}(ctx)

	return stream, func() {
		C.fclose(stream)
		w.Close()
		<-done
	}, nil
}

func freeCmdArgs(ap *C.struct_cmd_args_s) {
	if ap == nil {
		return
	}

	freeString(ap.sysname)

	C.free(unsafe.Pointer(ap.dfs_path))
	C.free(unsafe.Pointer(ap.dfs_prefix))

	if ap.props != nil {
		C.daos_prop_free(ap.props)
	}
	if ap.dm_args != nil {
		C.free_daos_alloc(unsafe.Pointer(ap.dm_args))
	}
	if ap.fs_copy_stats != nil {
		C.free_daos_alloc(unsafe.Pointer(ap.fs_copy_stats))
	}

	C.free(unsafe.Pointer(ap))
}

func freeDaosStr(str *C.char) {
	if str == nil {
		return
	}
	C.free_daos_alloc(unsafe.Pointer(str))
}

func allocCmdArgs(log logging.Logger) (ap *C.struct_cmd_args_s, cleanFn func(), err error) {
	// allocate the struct using C memory to avoid any issues with Go GC
	ap = (*C.struct_cmd_args_s)(C.calloc(1, C.sizeof_struct_cmd_args_s))
	C.init_op_vals(ap)
	ap.sysname = C.CString(build.DefaultSystemName)

	ctx, cancel := context.WithCancel(context.Background())
	outStream, outCleanup, err := createWriteStream(ctx, log.Info)
	if err != nil {
		freeCmdArgs(ap)
		cancel()
		return nil, nil, err
	}
	ap.outstream = outStream

	errStream, errCleanup, err := createWriteStream(ctx, log.Error)
	if err != nil {
		outCleanup()
		freeCmdArgs(ap)
		cancel()
		return nil, nil, err
	}
	ap.errstream = errStream

	return ap, func() {
		outCleanup()
		errCleanup()
		freeCmdArgs(ap)
		cancel()
	}, nil
}

type daosCaller interface {
	initDAOS() (func(), error)
}

type daosCmd struct {
	cmdutil.NoArgsCmd
	cmdutil.JSONOutputCmd
	cmdutil.LogCmd
}

func (dc *daosCmd) initDAOS() (func(), error) {
	if rc := C.daos_init(); rc != 0 {
		// Do some inspection of the RC to display an informative error to the user
		// e.g. "No DAOS Agent detected"...
		return nil, errors.Wrap(daosError(rc), "daos_init() failed")
	}

	return func() {
		if rc := C.daos_fini(); rc != 0 {
			dc.Errorf("daos_fini() failed: %s", daosError(rc))
		}
	}, nil
}

func initDaosDebug() (func(), error) {
	if rc := C.daos_debug_init(nil); rc != 0 {
		return nil, errors.Wrap(daosError(rc), "daos_debug_init() failed")
	}

	return func() {
		C.daos_debug_fini()
	}, nil
}

func resolveDunsPath(path string, ap *C.struct_cmd_args_s) error {
	if path == "" {
		return errors.New("empty path")
	}
	if ap == nil {
		return errors.New("nil ap")
	}

	ap.path = C.CString(path)
	defer freeString(ap.path)

	rc := C.resolve_duns_path(ap)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err, "failed to resolve path %s", path)
	}

	return nil
}

// _writeDunsPath is a test helper for creating a DUNS EA on a path.
func _writeDunsPath(path, ct string, poolUUID uuid.UUID, contUUID uuid.UUID) error {
	attrStr := fmt.Sprintf(C.DUNS_XATTR_FMT, ct, poolUUID.String(), contUUID.String())

	cPath := C.CString(path)
	defer freeString(cPath)
	cAttrStr := C.CString(attrStr)
	defer freeString(cAttrStr)
	cAttrName := C.CString(C.DUNS_XATTR_NAME)
	defer freeString(cAttrName)
	if err := dfsError(C.lsetxattr(cPath, cAttrName, unsafe.Pointer(cAttrStr), C.size_t(len(attrStr)+1), 0)); err != nil {
		return errors.Wrapf(err, "failed to set xattr on %s", path)
	}

	return nil
}
