//
// (C) Copyright 2021-2024 Intel Corporation.
// (C) Copyright 2025 Google LLC
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
	"github.com/daos-stack/daos/src/control/lib/daos/api"
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
	ap->sysname = NULL;
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

func srvBuildInfo() (*build.Info, error) {
	var major uint32
	var minor uint32
	var patch uint32
	var tagPtr *C.char

	rc := C.dc_mgmt_srv_version((*C.uint)(&major), (*C.uint)(&minor), (*C.uint)(&patch), &tagPtr)
	if err := daosError(rc); err != nil {
		return nil, err
	}
	tagStr := C.GoString(tagPtr)

	return &build.Info{
		Name:      build.ControlPlaneName,
		Version:   (&build.Version{Major: int(major), Minor: int(minor), Patch: int(patch)}).String(),
		BuildInfo: tagStr,
	}, nil
}

func daosError(rc C.int) error {
	return daos.ErrorFromRC(int(rc))
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

func uuidFromC(cUUID C.uuid_t) (uuid.UUID, error) {
	return uuid.FromBytes(C.GoBytes(unsafe.Pointer(&cUUID[0]), C.int(len(cUUID))))
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

	C.free(unsafe.Pointer(ap.dfs_path))
	C.free(unsafe.Pointer(ap.dfs_prefix))

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

type sysCmd struct {
	SysName string
}

func (sc *sysCmd) setSysName(sysName string) {
	sc.SysName = sysName
}

type daosCmd struct {
	cmdutil.NoArgsCmd
	cmdutil.JSONOutputCmd
	cmdutil.LogCmd
	sysCmd
	apiProvider *api.Provider
}

func (dc *daosCmd) initDAOS() (func(), error) {
	provider, err := api.NewProvider(dc.Logger, false)
	if err != nil {
		return func() {}, err
	}
	dc.apiProvider = provider

	return provider.Cleanup, nil
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

func attrListFromNames(names []string) daos.AttributeList {
	attrs := make(daos.AttributeList, len(names))
	for i, name := range names {
		attrs[i] = &daos.Attribute{Name: name}
	}
	return attrs
}
