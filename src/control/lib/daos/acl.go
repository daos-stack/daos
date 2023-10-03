//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import "unsafe"

/*
#include <stdlib.h>

#include <daos_security.h>
*/
import "C"

const (
	// ACLPrincipalMaxLen is the maximum length of a principal string.
	ACLPrincipalMaxLen = C.DAOS_ACL_MAX_PRINCIPAL_LEN
)

// ACLPrincipalIsValid returns true if the principal is valid.
func ACLPrincipalIsValid(principal string) bool {
	cPrincipal := C.CString(principal)
	defer C.free(unsafe.Pointer(cPrincipal))
	return bool(C.daos_acl_principal_is_valid(cPrincipal))
}
