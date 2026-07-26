//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build test_stubs

// Test shim: _test.go files in this package can't `import "C"` (Go disallows
// cgo in test files), so every cgo-touching test helper lives in a non-test
// file. Shared conversion/fixture helpers are here; per-export drivers live
// in the *_test_helpers.go file named for the export file they exercise,
// mirroring lib/daos/api's per-domain stub layout.

package main

/*
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <uuid/uuid.h>
#include <daos_types.h>
#include <daos_prop.h>
#include <gurt/common.h>

// cgo can't write into the dpe_type/dpe_val/dpe_str union directly.
static void
set_prop_entry_val(daos_prop_t *prop, uint32_t idx, uint32_t type, uint64_t val)
{
	prop->dpp_entries[idx].dpe_type = type;
	prop->dpp_entries[idx].dpe_val  = val;
}

static void
set_prop_entry_str(daos_prop_t *prop, uint32_t idx, uint32_t type, const char *str)
{
	prop->dpp_entries[idx].dpe_type = type;
	D_STRNDUP(prop->dpp_entries[idx].dpe_str, str, DAOS_PROP_LABEL_MAX_LEN);
}
*/
import "C"
import (
	"runtime/cgo"
	"unsafe"

	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
)

// makeTestHandle creates a cgo.Handle for a test context with the given mock invoker.
func makeTestHandle(mi control.UnaryInvoker, log *logging.LeveledLogger) cgo.Handle {
	return cgo.NewHandle(newTestContext(mi, log))
}

// testPropEntry describes one daos_prop_t entry for a test. Exactly one of
// NumVal or StrVal is used depending on the property's storage kind (see
// testPropNum / testPropStr).
type testPropEntry struct {
	Type   uint32
	NumVal uint64
	StrVal string
	isStr  bool
}

func testPropNum(propType uint32, val uint64) testPropEntry {
	return testPropEntry{Type: propType, NumVal: val}
}

func testPropStr(propType uint32, val string) testPropEntry {
	return testPropEntry{Type: propType, StrVal: val, isStr: true}
}

// Re-exported from cgo so _test.go files can refer to these constants.
const (
	testPropPoLabel      = uint32(C.DAOS_PROP_PO_LABEL)
	testPropPoOwner      = uint32(C.DAOS_PROP_PO_OWNER)
	testPropPoOwnerGroup = uint32(C.DAOS_PROP_PO_OWNER_GROUP)
	testPropPoRedunFac   = uint32(C.DAOS_PROP_PO_REDUN_FAC)
	testPropPoScrubMode  = uint32(C.DAOS_PROP_PO_SCRUB_MODE)
	testPropPoACL        = uint32(C.DAOS_PROP_PO_ACL)
	testPropPoSvcList    = uint32(C.DAOS_PROP_PO_SVC_LIST)
)

// buildCProp allocates a daos_prop_t from entries. Caller must C.daos_prop_free
// the result when done.
func buildCProp(entries []testPropEntry) *C.daos_prop_t {
	prop := C.daos_prop_alloc(C.uint32_t(len(entries)))
	for i, e := range entries {
		if e.isStr {
			cs := C.CString(e.StrVal)
			C.set_prop_entry_str(prop, C.uint32_t(i), C.uint32_t(e.Type), cs)
			C.free(unsafe.Pointer(cs))
		} else {
			C.set_prop_entry_val(prop, C.uint32_t(i), C.uint32_t(e.Type), C.uint64_t(e.NumVal))
		}
	}
	return prop
}

// cUUID builds a C uuid_t from a Go UUID. Returns the value; callers take its
// address via &... when passing to a function that wants *C.uuid_t.
func cUUID(u uuid.UUID) C.uuid_t {
	var out C.uuid_t
	copyUUIDToC(u, &out)
	return out
}

// cString wraps C.CString with a NULL-on-empty convention and a small closure
// for deferred freeing.
func cString(s string) (*C.char, func()) {
	if s == "" {
		return nil, func() {}
	}
	cs := C.CString(s)
	return cs, func() { C.free(unsafe.Pointer(cs)) }
}

// goCString reads a null-terminated C string pointer into a Go string.
func goCString(p *C.char) string {
	if p == nil {
		return ""
	}
	return C.GoString(p)
}
