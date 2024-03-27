package client

/*
#include <stdlib.h>

#include <daos_cont.h>
#include <daos_prop.h>

#include "util.h"

void
free_ace_list(char **str, size_t str_count)
{
	int i;

	for (i = 0; i < str_count; i++)
		D_FREE(str[i]);
	D_FREE(str);
}
*/
import "C"
import (
	"strings"
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/control"
)

func getAclStrings(e *C.struct_daos_prop_entry) (out []string) {
	acl := (*C.struct_daos_acl)(C.get_dpe_val_ptr(e))
	if acl == nil {
		return
	}

	var aces **C.char
	var acesNr C.size_t

	rc := C.daos_acl_to_strs(acl, &aces, &acesNr)
	if err := daosError(rc); err != nil || aces == nil {
		return
	}
	defer C.free_ace_list(aces, acesNr)

	for _, ace := range (*[1 << 30]*C.char)(unsafe.Pointer(aces))[:acesNr:acesNr] {
		out = append(out, C.GoString(ace))
	}

	return
}

func aclStringer(e *C.struct_daos_prop_entry, name string) string {
	if e == nil {
		return propNotFound(name)
	}

	return strings.Join(getAclStrings(e), ", ")
}

func aclToC(acl *control.AccessControlList) (*C.struct_daos_acl, func(), error) {
	aceStrs := make([]*C.char, len(acl.Entries))
	for i, ace := range acl.Entries {
		aceStrs[i] = C.CString(ace)
		defer C.free(unsafe.Pointer(aceStrs[i]))
	}

	var cACL *C.struct_daos_acl
	rc := C.daos_acl_from_strs(&aceStrs[0], C.ulong(len(aceStrs)), &cACL)
	if err := daosError(rc); err != nil {
		return nil, nil, errors.Wrapf(err,
			"unable to create ACL structure")
	}

	return cACL, func() { C.daos_acl_free(cACL) }, nil
}
