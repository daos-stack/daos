//
// (C) Copyright 2021-2024 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

/*
#include "util.h"

uid_t
invalid_uid(void)
{
	return (uid_t)-1;
}

gid_t
invalid_gid(void)
{
	return (gid_t)-1;
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
	"github.com/daos-stack/daos/src/control/lib/daos"
)

type aclCmd struct {
	existingContainerCmd
}

func (cmd *aclCmd) getACL() (*control.AccessControlList, error) {
	aclPropNames := []string{
		daos.ContainerPropACL.String(),
		daos.ContainerPropOwner.String(),
		daos.ContainerPropGroup.String(),
	}
	propList, err := cmd.container.GetProperties(cmd.MustLogCtx(), aclPropNames...)
	if err != nil {
		return nil, err
	}
	defer propList.Free()

	return convertACLProps(propList.Properties()), nil
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

	cleanup, err := cmd.resolveAndOpen(daos.ContainerOpenFlagReadWrite, ap)
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

	acl, err := cmd.getACL()
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

	cleanup, err := cmd.resolveAndOpen(daos.ContainerOpenFlagReadWrite, ap)
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

	acl, err := cmd.getACL()
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

	cleanup, err := cmd.resolveAndOpen(daos.ContainerOpenFlagReadWrite, ap)
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

	acl, err := cmd.getACL()
	if err != nil {
		return errors.Wrap(err, "unable to fetch updated ACL")
	}

	return cmd.outputACL(os.Stdout, acl, false)
}

func convertACLProps(props []*daos.ContainerProperty) (acl *control.AccessControlList) {
	acl = new(control.AccessControlList)

	for _, prop := range props {
		switch prop.Type {
		case daos.ContainerPropACL:
			acl.Entries = strings.Split(prop.StringValue(), ", ")
		case daos.ContainerPropOwner:
			acl.Owner = prop.StringValue()
		case daos.ContainerPropGroup:
			acl.OwnerGroup = prop.StringValue()
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

	cleanup, err := cmd.resolveAndOpen(daos.ContainerOpenFlagReadOnly, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	acl, err := cmd.getACL()
	if err != nil {
		return errors.Wrapf(err, "failed to query ACL for container %s", cmd.ContainerID())
	}

	if cmd.File != "" {
		flags := os.O_CREATE | os.O_WRONLY
		if !cmd.Force {
			flags |= os.O_EXCL
		}

		output, err := os.OpenFile(cmd.File, flags, 0644)
		if err != nil {
			return errors.Wrap(err,
				"failed to open ACL output file")
		}
		defer output.Close()

		if _, err := fmt.Fprint(output, control.FormatACL(acl, cmd.Verbose)); err != nil {
			return errors.Wrapf(err, "failed to write ACL output file")
		}
	}

	var buf strings.Builder
	if err := cmd.outputACL(&buf, acl, cmd.Verbose); err != nil {
		return err
	}
	cmd.Info(buf.String())
	return nil
}

type containerSetOwnerCmd struct {
	existingContainerCmd

	User    string `long:"user" short:"u" description:"user who will own the container"`
	Uid     *int   `long:"uid" description:"with --no-check, specify a uid for POSIX container ownership"`
	Group   string `long:"group" short:"g" description:"group who will own the container"`
	Gid     *int   `long:"gid" description:"with --no-check, specify a gid for POSIX container ownership"`
	NoCheck bool   `long:"no-check" description:"don't check whether the user/group exists on the local system"`
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

	cleanup, err := cmd.resolveAndOpen(daos.ContainerOpenFlagReadWrite, ap)
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

	propList, err := cmd.container.GetProperties(cmd.MustLogCtx(), daos.ContainerPropLayoutType.String())
	if err != nil {
		return err
	}
	defer propList.Free()

	entries := propList.Properties()

	lType := daos.ContainerLayout(entries[0].GetValue())
	if lType == daos.ContainerLayoutPOSIX {
		uid := C.invalid_uid()
		gid := C.invalid_gid()
		if cmd.NoCheck {
			if cmd.User != "" {
				if cmd.Uid == nil {
					return errors.New("POSIX container requires --uid to use --no-check")
				}
				uid = C.uid_t(*cmd.Uid)
			}

			if cmd.Group != "" {
				if cmd.Gid == nil {
					return errors.New("POSIX container requires --gid to use --no-check")
				}
				gid = C.gid_t(*cmd.Gid)
			}
		} else if cmd.Uid != nil || cmd.Gid != nil {
			return errors.New("--no-check is required to use the --uid and --gid options")
		}

		if err := dfsError(C.dfs_cont_set_owner(ap.cont, user, uid, group, gid)); err != nil {
			return errors.Wrapf(err, "failed to set owner for POSIX container %s",
				cmd.ContainerID())
		}
	} else {
		if cmd.Uid != nil || cmd.Gid != nil {
			return errors.New("--uid and --gid options apply for POSIX containers only")
		}

		var rc C.int
		if cmd.NoCheck {
			rc = C.daos_cont_set_owner_no_check(ap.cont, user, group, nil)
		} else {
			rc = C.daos_cont_set_owner(ap.cont, user, group, nil)
		}
		if err := daosError(rc); err != nil {
			return errors.Wrapf(err, "failed to set owner for container %s",
				cmd.ContainerID())
		}
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
