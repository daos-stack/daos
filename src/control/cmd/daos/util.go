//
// (C) Copyright 2018-2021 Intel Corporation.
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

func createWriteStream(prefix string, printLn func(line string)) (*C.FILE, func(), error) {
	// Create a FILE object for the handler to use for
	// printing output or errors, and call the callback
	// for each line.
	r, w, err := os.Pipe()
	if err != nil {
		return nil, nil, err
	}

	write := C.CString("w")
	defer C.free(unsafe.Pointer(write))
	stream := C.fdopen(C.int(w.Fd()), C.CString("w"))
	if stream == nil {
		return nil, nil, errors.New("fdopen() failed")
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
	initDAOS(logging.Logger) (func(), error)
}

type daosCmd struct {
	jsonOutputCmd
	logCmd
}

func (dc *daosCmd) initDAOS(log logging.Logger) (func(), error) {
	if rc := C.daos_init(); rc != 0 {
		// Do some inspection of the RC to display an informative error to the user
		// e.g. "No DAOS Agent detected"...
		return nil, errors.Wrap(daosError(rc), "daos_init() failed")
	}

	return func() {
		if rc := C.daos_fini(); rc != 0 {
			// Not much to do I guess?
			log.Errorf("daos_fini() failed: %s", daosError(rc))
		}
	}, nil
}
