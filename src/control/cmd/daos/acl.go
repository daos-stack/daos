//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

/*
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
	"fmt"
	"io"
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

type aclCmd struct {
	existingContainerCmd
}

func (cmd *aclCmd) getACL(ap *C.struct_cmd_args_s) (*control.AccessControlList, error) {
	props, cleanup, err := getContAcl(ap.cont)
	if err != nil {
		return nil, err
	}
	defer cleanup()

	return convertACLProps(props), nil
}

func (cmd *aclCmd) outputACL(out io.Writer, acl *control.AccessControlList, verbose bool) error {
	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(acl, nil)
	}

	_, err := fmt.Fprint(out, control.FormatACL(acl, verbose))
	return err
}

type containerOverwriteACLCmd struct {
	aclCmd

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

	cACL, cleanupACL, err := aclFileToC(cmd.File)
	if err != nil {
		return err
	}
	defer cleanupACL()

	rc := C.daos_cont_overwrite_acl(ap.cont, cACL, nil)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err,
			"failed to overwrite ACL for container %s",
			cmd.ContainerID())
	}

	acl, err := cmd.getACL(ap)
	if err != nil {
		return errors.Wrap(err, "unable to fetch updated ACL")
	}

	return cmd.outputACL(os.Stdout, acl, false)
}

func aclFileToC(aclFile string) (*C.struct_daos_acl, func(), error) {
	acl, err := control.ReadACLFile(aclFile)
	if err != nil {
		return nil, nil, err
	}

	return aclToC(acl)
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

type containerUpdateACLCmd struct {
	aclCmd

	Entry string `long:"entry" short:"e"`
	File  string `long:"acl-file" short:"f"`
}

func (cmd *containerUpdateACLCmd) Execute(args []string) error {
	var cACL *C.struct_daos_acl
	var cleanupACL func()
	var err error

	switch {
	case cmd.Entry != "" && cmd.File != "":
		return errors.New("only one of --entry or --acl-file may be supplied")
	case cmd.Entry != "":
		acl := &control.AccessControlList{
			Entries: []string{cmd.Entry},
		}
		cACL, cleanupACL, err = aclToC(acl)
	case cmd.File != "":
		cACL, cleanupACL, err = aclFileToC(cmd.File)
	default:
		return errors.New("one of --entry or --acl-file must be supplied")
	}

	if err != nil {
		return err
	}
	defer cleanupACL()

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

	rc := C.daos_cont_update_acl(ap.cont, cACL, nil)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err,
			"failed to update ACL for container %s",
			cmd.ContainerID())
	}

	acl, err := cmd.getACL(ap)
	if err != nil {
		return errors.Wrap(err, "unable to fetch updated ACL")
	}

	return cmd.outputACL(os.Stdout, acl, false)
}

type containerDeleteACLCmd struct {
	aclCmd

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

	cPrincipal := C.CString(cmd.Principal)
	defer C.free(unsafe.Pointer(cPrincipal))
	var cType C.enum_daos_acl_principal_type
	var cName *C.char
	rc := C.daos_acl_principal_from_str(cPrincipal, &cType, &cName)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err, "unable to parse principal string %q", cmd.Principal)
	}

	rc = C.daos_cont_delete_acl(ap.cont, cType, cName, nil)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err,
			"failed to delete ACL for container %s",
			cmd.ContainerID())
	}

	acl, err := cmd.getACL(ap)
	if err != nil {
		return errors.Wrap(err, "unable to fetch updated ACL")
	}

	return cmd.outputACL(os.Stdout, acl, false)
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
	aclCmd

	File    string `long:"outfile" short:"O" description:"write ACL to file"`
	Force   bool   `long:"force" short:"f" description:"overwrite existing outfile"`
	Verbose bool   `long:"verbose" short:"V" description:"show verbose output"`
}

func (cmd *containerGetACLCmd) Execute(args []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RO, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	acl, err := cmd.getACL(ap)
	if err != nil {
		return errors.Wrapf(err, "failed to query ACL for container %s", cmd.contUUID)
	}

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

	return cmd.outputACL(output, acl, cmd.Verbose)
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

	var user *C.char
	var group *C.char
	if cmd.User != "" {
		if !strings.ContainsRune(cmd.User, '@') {
			cmd.User += "@"
		}
		user = C.CString(cmd.User)
		defer C.free(unsafe.Pointer(user))
	}
	if cmd.Group != "" {
		if !strings.ContainsRune(cmd.Group, '@') {
			cmd.Group += "@"
		}
		group = C.CString(cmd.Group)
		defer C.free(unsafe.Pointer(group))
	}

	rc := C.daos_cont_set_owner(ap.cont, user, group, nil)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err,
			"failed to set owner for container %s",
			cmd.ContainerID())
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(nil, nil)
	}

	var contID string
	if cmd.ContainerID().Empty() {
		contID = cmd.Path
	} else {
		contID = cmd.ContainerID().String()
	}

	cmd.Infof("Successfully set owner for container %s", contID)
	return nil
}
