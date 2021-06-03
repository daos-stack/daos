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
	"strconv"
	"strings"
	"unsafe"

	"github.com/dustin/go-humanize"
	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
)

/*
// NB: There should only be one set of CFLAGS/LDFLAGS definitions
// for the whole package!
#cgo CFLAGS: -I${SRCDIR}/../../../utils
#cgo LDFLAGS: -lgurt -lcart -ldaos -ldaos_common -lduns -ldfs -luuid -ldaos_cmd_hdlrs

#include <sys/stat.h>
#include <daos.h>
#include <daos/common.h>

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
	defer freeString(cModeStr)
	out = C.fdopen(C.int(fd), cModeStr)
	if out == nil {
		return nil, errors.New("fdopen() failed")
	}
	return
}

func freeString(str *C.char) {
	if str == nil {
		return
	}
	C.free(unsafe.Pointer(str))
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

func freeCmdArgs(ap *C.struct_cmd_args_s) {
	if ap == nil {
		return
	}

	freeString(ap.sysname)
	C.free_daos_alloc(unsafe.Pointer(ap.dfs_path))

	if ap.props != nil {
		ap.props.dpp_nr = C.DAOS_PROP_ENTRIES_MAX_NR
		C.daos_prop_free(ap.props)
	}
}

func allocCmdArgs(log logging.Logger) (ap *C.struct_cmd_args_s, cleanFn func(), err error) {
	ap = &C.struct_cmd_args_s{}
	C.init_op_vals(ap)
	ap.sysname = C.CString(build.DefaultSystemName)

	outStream, outCleanup, err := createWriteStream("", log.Info)
	if err != nil {
		freeCmdArgs(ap)
		return nil, nil, err
	}
	ap.outstream = outStream

	errStream, errCleanup, err := createWriteStream("handler", log.Error)
	if err != nil {
		outCleanup()
		freeCmdArgs(ap)
		return nil, nil, err
	}
	ap.errstream = errStream

	return ap, func() {
		outCleanup()
		errCleanup()
		freeCmdArgs(ap)
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
		if rc := C.daos_fini(); rc != 0 {
			dc.log.Errorf("daos_fini() failed: %s", daosError(rc))
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

type epochRange struct {
	set   bool
	begin uint64
	end   uint64
}

func (er *epochRange) String() string {
	return fmt.Sprintf("%d-%d", er.begin, er.end)
}

func (er *epochRange) UnmarshalFlag(fv string) error {
	er.set = true
	n, err := fmt.Sscanf(fv, "%d-%d", &er.begin, &er.end)
	if err != nil {
		return err
	}
	if n != 2 {
		return errors.Errorf("range=%q must be in A-B form", fv)
	}
	if er.begin >= er.end {
		return errors.Errorf("range begin must be < end")
	}
	return nil
}

type chunkSize struct {
	set  bool
	size C.uint64_t
}

func (c *chunkSize) UnmarshalFlag(fv string) error {
	if fv == "" {
		return errors.New("empty chunk size")
	}

	size, err := humanize.ParseBytes(fv)
	if err != nil {
		return err
	}
	c.size = C.uint64_t(size)
	c.set = true

	return nil
}

type objectClass struct {
	set   bool
	class C.ushort
}

func (oc *objectClass) UnmarshalFlag(fv string) error {
	if fv == "" {
		return errors.New("empty object class")
	}

	cObjClass := C.CString(fv)
	defer freeString(cObjClass)

	oc.class = (C.ushort)(C.daos_oclass_name2id(cObjClass))
	if oc.class == C.OC_UNKNOWN {
		return errors.Errorf("unknown object class %q",
			fv)
	}
	oc.set = true

	return nil
}

type oidFlag struct {
	set bool
	oid C.daos_obj_id_t
}

func (f *oidFlag) String() string {
	return fmt.Sprintf("%d.%d", f.oid.hi, f.oid.lo)
}

func (f *oidFlag) UnmarshalFlag(fv string) error {
	parseErr := fmt.Sprintf("failed to parse %q as OID (expected HI.LO)", fv)
	comps := strings.Split(fv, ".")
	if len(comps) != 2 {
		return errors.New(parseErr)
	}

	val, err := strconv.ParseUint(comps[0], 10, 64)
	if err != nil {
		return errors.Wrap(err, parseErr)
	}
	f.oid.hi = C.uint64_t(val)
	val, err = strconv.ParseUint(comps[1], 10, 64)
	if err != nil {
		return errors.Wrap(err, parseErr)
	}
	f.oid.lo = C.uint64_t(val)

	f.set = true

	return nil
}
