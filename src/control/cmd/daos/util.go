//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"unsafe"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
)

/*
#cgo CFLAGS: -I${SRCDIR}/../../../utils
#cgo LDFLAGS: -lgurt -lcart -ldaos -ldaos_common

#include <stdlib.h>
#include <daos.h>
#include <daos/common.h>
#include <daos/debug.h>

#include "daos_hdlr.h"

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
	return drpc.DaosStatus(rc)
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
	defer C.free(unsafe.Pointer(cModeStr))
	out = C.fdopen(C.int(fd), cModeStr)
	if out == nil {
		return nil, errors.New("fdopen() failed")
	}
	return
}

func createWriteStream(prefix string, printLn func(line string)) (*C.FILE, func(), error) {
	// Create a FILE object for the handler to use for
	// printing output or errors, and call the callback
	// for each line.
	r, w, err := os.Pipe()
	if err != nil {
		return nil, nil, err
	}

	stream, err := fd2FILE(w.Fd(), "w")
	if err != nil {
		return nil, nil, err
	}

	go func(prefix string) {
		defer r.Close()
		defer w.Close()

		if prefix != "" {
			prefix = ": "
		}

		rdr := bufio.NewReader(r)
		for {
			line, err := rdr.ReadString('\n')
			if err != nil {
				if err != io.EOF {
					printLn(fmt.Sprintf("read err: %s", err))
				}
				return
			}
			printLn(fmt.Sprintf("%s%s", prefix, line))
		}
	}(prefix)

	return stream, func() {
		C.fclose(stream)
		C.sync()
	}, nil
}

func deallocCmdArgs(ap *C.struct_cmd_args_s) {
	if ap == nil {
		return
	}

	if ap.sysname != nil {
		C.free(unsafe.Pointer(ap.sysname))
	}

	if ap.props != nil {
		ap.props.dpp_nr = C.DAOS_PROP_ENTRIES_MAX_NR
		C.daos_prop_free(ap.props)
	}

	C.free(unsafe.Pointer(ap))
}

func allocCmdArgs(log logging.Logger) (ap *C.struct_cmd_args_s, cleanFn func(), err error) {
	ap = (*C.struct_cmd_args_s)(C.calloc(1, C.sizeof_struct_cmd_args_s))

	if ap == nil {
		return nil, nil, errors.New("unable to alloc cmd_args_s")
	}

	C.init_op_vals(ap)
	ap.sysname = C.CString(build.DefaultSystemName)

	outStream, outCleanup, err := createWriteStream("", log.Info)
	if err != nil {
		return nil, nil, err
	}
	ap.outstream = outStream

	errStream, errCleanup, err := createWriteStream("handler", log.Error)
	if err != nil {
		return nil, nil, err
	}
	ap.errstream = errStream

	return ap, func() {
		outCleanup()
		errCleanup()
		deallocCmdArgs(ap)
	}, nil
}

type daosCaller interface {
	initDAOS() (func(), error)
}

type daosCmd struct {
	jsonOutputCmd
	logCmd
}

func (dc *daosCmd) initDAOS() (func(), error) {
	if rc := C.daos_init(); rc != 0 {
		// Do some inspection of the RC to display an informative error to the user
		// e.g. "No DAOS Agent detected"...
		return nil, errors.Wrap(daosError(rc), "daos_init() failed")
	}

	return func() {
		C.daos_fini()
	}, nil
}

type labelOrUUID struct {
	UUID  uuid.UUID
	Label string
}

func (f labelOrUUID) Empty() bool {
	return !f.HasLabel() && !f.HasUUID()
}

func (f labelOrUUID) HasLabel() bool {
	return f.Label != ""
}

func (f labelOrUUID) HasUUID() bool {
	return f.UUID != uuid.Nil
}

func (f labelOrUUID) String() string {
	switch {
	case f.HasLabel():
		return f.Label
	case f.HasUUID():
		return f.UUID.String()
	default:
		return "<no label or uuid set>"
	}
}

func (f *labelOrUUID) UnmarshalFlag(fv string) error {
	uuid, err := uuid.Parse(fv)
	if err == nil {
		f.UUID = uuid
		return nil
	}

	f.Label = fv
	return nil
}
