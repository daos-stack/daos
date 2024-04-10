package client

import (
	"unsafe"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/daos"
)

/*
#include <stdlib.h>
#include <uuid/uuid.h>

#include <daos_prop.h>

#include "util.h"
*/
import "C"

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

func freeString(s *C.char) {
	C.free(unsafe.Pointer(s))
}

type propSlice []C.struct_daos_prop_entry

func (ps propSlice) getEntry(pType C.uint32_t) *C.struct_daos_prop_entry {
	for _, entry := range ps {
		if entry.dpe_type == pType {
			return &entry
		}
	}
	return nil
}

func createPropSlice(props *C.daos_prop_t, numProps int) propSlice {
	// Create a Go slice backed by the props array for easier
	// iteration.
	return unsafe.Slice((*C.struct_daos_prop_entry)(unsafe.Pointer(props.dpp_entries)), numProps)
}

func allocProps(numProps int) (props *C.daos_prop_t, entries propSlice, err error) {
	props = C.daos_prop_alloc(C.uint(numProps))
	if props == nil {
		return nil, nil, errors.Wrap(daos.NoMemory, "failed to allocate properties list")
	}

	props.dpp_nr = 0
	entries = createPropSlice(props, numProps)

	return
}

func freeProps(props *C.daos_prop_t) {
	C.daos_prop_free(props)
}

// dupePropEntry copies an entry into Go-managed memory so that the
// source entry can be safely deallocated.
//
// NB: This helper is loosely based on C.daos_prop_entry_copy(), but
// is not intended to be as comprehensive. It should be used for getting
// a property list into Go memory in order to cross API boundaries without
// requiring memory management outside of this API.
func dupePropEntry(src, dest *C.struct_daos_prop_entry) error {
	if src == nil || dest == nil {
		return errors.Wrap(daos.InvalidInput, "source and dest entries must not be nil")
	}

	dest.dpe_type = src.dpe_type

	switch src.dpe_type {
	case C.DAOS_PROP_PO_ACL, C.DAOS_PROP_CO_ACL:
		cPtr := C.get_dpe_val_ptr(src)
		if cPtr == nil {
			return errors.Wrap(daos.InvalidInput, "bad prop value pointer")
		}
		goBuf := C.GoBytes(cPtr, C.int(C.daos_acl_get_size((*C.struct_daos_acl)(cPtr))))
		C.set_dpe_val_ptr(dest, unsafe.Pointer(&goBuf[0]))
	default:
		// If there's a C string in the entry, copy it (along with the terminator)
		// into a Go buffer.
		if C.daos_prop_has_str(src) {
			cStr := C.get_dpe_str(src)
			if cStr == nil {
				return errors.Wrap(daos.InvalidInput, "bad prop string")
			}
			goBuf := C.GoBytes(unsafe.Pointer(cStr), C.int(C.strlen(cStr)+1))
			C.set_dpe_str(dest, (*C.char)(unsafe.Pointer(&goBuf[0])))
			return nil
		}

		// No buffers to duplicate; just copy the uint64 value.
		C.set_dpe_val(dest, C.get_dpe_val(src))
	}

	return nil
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
