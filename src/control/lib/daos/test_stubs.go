//
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build test_stubs
// +build test_stubs

package daos

import (
	"testing"
	"unsafe"
)

/*
#include <daos.h>
#include <daos/security.h>
*/
import "C"

func fillTestRootObjectsProp(t *testing.T, prop *ContainerProperty, roots ...ObjectID) {
	t.Helper()

	cRoots := C.struct_daos_prop_co_roots{}
	oidSlice := unsafe.Slice(&cRoots.cr_oids[0], 4)

	if len(roots) > len(oidSlice) {
		t.Fatalf("roots > %d", len(oidSlice))
	}

	for i, oid := range roots {
		oidSlice[i] = C.daos_obj_id_t(oid)
	}

	prop.SetValuePtr(unsafe.Pointer(&cRoots))
}

func fillTestACLProp(t *testing.T, prop *ContainerProperty, aclStrs ...string) func() {
	t.Helper()

	// TODO: Migrate this to the API when we pull all of the ACL stuff in.
	aceStrs := make([]*C.char, len(aclStrs))
	for i, ace := range aclStrs {
		aceStrs[i] = C.CString(ace)
		defer C.free(unsafe.Pointer(aceStrs[i]))
	}

	var cACL *C.struct_daos_acl
	if rc := C.daos_acl_from_strs(&aceStrs[0], C.ulong(len(aceStrs)), &cACL); rc != 0 {
		t.Fatalf("daos_acl_from_strs failed: %s", Status(rc))
	}

	prop.SetValuePtr(unsafe.Pointer(cACL))
	return func() {
		C.daos_acl_free(cACL)
	}
}
