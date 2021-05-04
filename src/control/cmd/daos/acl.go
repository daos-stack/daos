//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

/*
#cgo LDFLAGS: -ldaos_common

#include <daos.h>
#include <daos_security.h>
#include <gurt/common.h>

#include "property.h"

void
free_strings(char **str, size_t str_count)
{
	int i;

	for (i = 0; i < str_count; i++)
		D_FREE(str[i]);
}
*/
import "C"
import (
	"fmt"
	"strings"
	"unsafe"

	"github.com/pkg/errors"
)

func getAclStrings(e *C.struct_daos_prop_entry) (out []string) {
	acl := (*C.struct_daos_acl)(C.get_dpe_val_ptr(e))
	if acl == nil {
		return
	}

	var aces **C.char
	var acesNr C.size_t

	rc := C.daos_acl_to_strs(acl, &aces, &acesNr)
	if err := daosError(rc); err != nil {
		return
	}
	defer C.free_strings(aces, acesNr)

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

func getContAcl(hdl C.daos_handle_t) ([]*property, func(), error) {
	expPropNr := 3 // ACL, user, group

	var props *C.daos_prop_t
	rc := C.daos_cont_get_acl(hdl, &props, nil)
	if err := daosError(rc); err != nil {
		return nil, nil, err
	}
	if props.dpp_nr != C.uint(expPropNr) {
		return nil, nil, errors.Errorf("invalid number of ACL props (%d != %d)",
			props.dpp_nr, expPropNr)
	}

	entries := createPropSlice(props, expPropNr)
	outProps := make([]*property, len(entries))
	for i, entry := range entries {
		switch entry.dpe_type {
		case C.DAOS_PROP_CO_ACL:
			outProps[i] = &property{
				entry:       &entries[i],
				toString:    aclStringer,
				Name:        "acl",
				Description: "Access Control List",
			}
		case C.DAOS_PROP_CO_OWNER:
			outProps[i] = &property{
				entry:       &entries[i],
				toString:    strValStringer,
				Name:        "user",
				Description: "User",
			}
		case C.DAOS_PROP_CO_OWNER_GROUP:
			outProps[i] = &property{
				entry:       &entries[i],
				toString:    strValStringer,
				Name:        "group",
				Description: "Group",
			}
		}
	}

	return outProps, func() { C.daos_prop_free(props) }, nil
}

type containerOverwriteACLCmd struct {
	existingContainerCmd
}

func (cmd *containerOverwriteACLCmd) Execute(args []string) error {
	return nil
}

type containerUpdateACLCmd struct {
	existingContainerCmd
}

func (cmd *containerUpdateACLCmd) Execute(args []string) error {
	return nil
}

type containerDeleteACLCmd struct {
	existingContainerCmd
}

func (cmd *containerDeleteACLCmd) Execute(args []string) error {
	return nil
}

type containerGetACLCmd struct {
	existingContainerCmd
}

func (cmd *containerGetACLCmd) Execute(args []string) error {
	cleanup, err := cmd.resolveAndConnect(nil)
	if err != nil {
		return err
	}
	defer cleanup()

	aclProps, cleanupAcl, err := getContAcl(cmd.cContHandle)
	if err != nil {
		return errors.Wrapf(err,
			"failed to query ACL for container %s", cmd.contUUID)
	}
	defer cleanupAcl()

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(aclProps, nil)
	}

	var bld strings.Builder
	printProperties(&bld, fmt.Sprintf("ACL for container %s", cmd.contUUID),
		aclProps...)

	cmd.log.Info(bld.String())

	return nil
}

type containerSetOwnerCmd struct {
	existingContainerCmd
}

func (cmd *containerSetOwnerCmd) Execute(args []string) error {
	return nil
}
