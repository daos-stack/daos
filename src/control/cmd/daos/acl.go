//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

/*
#include "util.h"

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
	"os"
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
	if props == nil {
		return nil, nil, errors.New("nil ACL props")
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

	File string `long:"acl-file" short:"f" required:"1"`
}

func (cmd *containerOverwriteACLCmd) Execute(args []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RW, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	ap.aclfile = C.CString(cmd.File)
	defer C.free(unsafe.Pointer(ap.aclfile))

	rc := C.cont_overwrite_acl_hdlr(ap)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err,
			"failed to overwrite ACL for container %s",
			cmd.ContainerID())
	}

	return nil
}

type containerUpdateACLCmd struct {
	existingContainerCmd

	Entry string `long:"entry" short:"e"`
	File  string `long:"acl-file" short:"f"`
}

func (cmd *containerUpdateACLCmd) Execute(args []string) error {
	if cmd.Entry != "" && cmd.File != "" {
		return errors.New("only one of entry or acl-file may be supplied")
	}

	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RW, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	switch {
	case cmd.Entry != "":
		ap.entry = C.CString(cmd.Entry)
		defer C.free(unsafe.Pointer(ap.entry))
	case cmd.File != "":
		ap.aclfile = C.CString(cmd.File)
		defer C.free(unsafe.Pointer(ap.aclfile))
	}

	rc := C.cont_update_acl_hdlr(ap)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err,
			"failed to update ACL for container %s",
			cmd.ContainerID())
	}

	return nil
}

type containerDeleteACLCmd struct {
	existingContainerCmd

	Principal string `long:"principal" short:"P" required:"1"`
}

func (cmd *containerDeleteACLCmd) Execute(args []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RW, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	ap.principal = C.CString(cmd.Principal)
	defer C.free(unsafe.Pointer(ap.principal))

	rc := C.cont_delete_acl_hdlr(ap)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err,
			"failed to delete ACL for container %s",
			cmd.ContainerID())
	}

	return nil
}

func convertACLProps(props []*property) (acl *control.AccessControlList) {
	acl = new(control.AccessControlList)

	for _, prop := range props {
		switch prop.entry.dpe_type {
		case C.DAOS_PROP_CO_ACL:
			acl.Entries = strings.Split(prop.toString(prop.entry, "acls"), ", ")
		case C.DAOS_PROP_CO_OWNER:
			acl.Owner = prop.toString(prop.entry, "owner")
		case C.DAOS_PROP_CO_OWNER_GROUP:
			acl.OwnerGroup = prop.toString(prop.entry, "group")
		}
	}

	return
}

type containerGetACLCmd struct {
	existingContainerCmd

	File    string `long:"outfile" short:"O" description:"write ACL to file"`
	Force   bool   `long:"force" short:"f" description:"overwrite existing outfile"`
	Verbose bool   `long:"verbose" short:"V" description:"show verbose output"`
}

func (cmd *containerGetACLCmd) Execute(args []string) error {
	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RO, nil)
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

	acl := convertACLProps(aclProps)

	output := os.Stdout
	if cmd.File != "" {
		flags := os.O_CREATE | os.O_WRONLY
		if !cmd.Force {
			flags |= os.O_EXCL
		}

		output, err = os.OpenFile(cmd.File, flags, 0644)
		if err != nil {
			return errors.Wrap(err,
				"failed to open ACL output file")
		}
		defer output.Close()
	}

	if cmd.jsonOutputEnabled() {
		cmd.wroteJSON.SetTrue()
		return outputJSON(output, acl, nil)
	}

	_, err = fmt.Fprintf(output, control.FormatACL(acl, cmd.Verbose))
	return err
}

type containerSetOwnerCmd struct {
	existingContainerCmd

	User  string `long:"user" short:"u" description:"user who will own the container"`
	Group string `long:"group" short:"g" description:"group who will own the container"`
}

func (cmd *containerSetOwnerCmd) Execute(args []string) error {
	if cmd.User == "" && cmd.Group == "" {
		return errors.New("at least one of user or group must be supplied")
	}

	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RW, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	if cmd.User != "" {
		ap.user = C.CString(cmd.User)
		defer C.free(unsafe.Pointer(ap.user))
	}
	if cmd.Group != "" {
		ap.group = C.CString(cmd.Group)
		defer C.free(unsafe.Pointer(ap.group))
	}

	rc := C.cont_set_owner_hdlr(ap)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err,
			"failed to set owner for container %s",
			cmd.ContainerID())
	}

	return nil
}
