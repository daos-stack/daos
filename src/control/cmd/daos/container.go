//
// (C) Copyright 2021-2024 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"unsafe"

	"github.com/google/uuid"
	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/daos/pretty"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/daos/api"
	"github.com/daos-stack/daos/src/control/lib/ui"
	"github.com/daos-stack/daos/src/control/logging"
)

/*
#include "util.h"

*/
import "C"

type containerCmd struct {
	Create      containerCreateCmd      `command:"create" description:"create a container"`
	Link        containerLinkCmd        `command:"link" description:"create a UNS link to an existing container"`
	List        containerListCmd        `command:"list" alias:"ls" description:"list all containers in pool"`
	Destroy     containerDestroyCmd     `command:"destroy" description:"destroy a container"`
	ListObjects containerListObjectsCmd `command:"list-objects" alias:"list-obj" description:"list all objects in container"`
	Query       containerQueryCmd       `command:"query" description:"query a container"`
	Stat        containerStatCmd        `command:"stat" description:"get container statistics"`
	Clone       containerCloneCmd       `command:"clone" description:"clone a container"`
	Check       containerCheckCmd       `command:"check" description:"check objects' consistency in a container"`
	Evict       containerEvictCmd       `command:"evict" description:"Evict open handles on a container"`

	ListAttributes  containerListAttrsCmd `command:"list-attr" alias:"list-attrs" alias:"lsattr" description:"list container user-defined attributes"`
	DeleteAttribute containerDelAttrCmd   `command:"del-attr" alias:"delattr" description:"delete container user-defined attribute"`
	GetAttribute    containerGetAttrCmd   `command:"get-attr" alias:"getattr" description:"get container user-defined attribute"`
	SetAttribute    containerSetAttrCmd   `command:"set-attr" alias:"setattr" description:"set container user-defined attribute"`

	GetProperty containerGetPropCmd `command:"get-prop" alias:"getprop" description:"get container properties"`
	SetProperty containerSetPropCmd `command:"set-prop" alias:"setprop" description:"set container properties"`

	GetACL       containerGetACLCmd       `command:"get-acl" description:"get a container's ACL"`
	OverwriteACL containerOverwriteACLCmd `command:"overwrite-acl" alias:"replace" description:"replace a container's ACL"`
	UpdateACL    containerUpdateACLCmd    `command:"update-acl" description:"update a container's ACL"`
	DeleteACL    containerDeleteACLCmd    `command:"delete-acl" description:"delete a container's ACL"`
	SetOwner     containerSetOwnerCmd     `command:"set-owner" alias:"chown" description:"change ownership for a container"`

	CreateSnapshot  containerSnapCreateCmd       `command:"create-snap" alias:"snap" description:"create container snapshot"`
	DestroySnapshot containerSnapDestroyCmd      `command:"destroy-snap" description:"destroy container snapshot"`
	ListSnapshots   containerSnapListCmd         `command:"list-snap" alias:"list-snaps" description:"list container snapshots"`
	Rollback        containerSnapshotRollbackCmd `command:"rollback" description:"roll back container to specified snapshot"`
}

type containerBaseCmd struct {
	poolBaseCmd
	container *api.ContainerHandle

	// deprecated params -- gradually remove in favor of ContainerHandle
	contLabel   string
	contUUID    uuid.UUID
	cContHandle C.daos_handle_t
}

func (cmd *containerBaseCmd) openContainer(openFlags daos.ContainerOpenFlag) error {
	openFlags |= daos.ContainerOpenFlagForce
	if (openFlags & daos.ContainerOpenFlagReadOnly) != 0 {
		openFlags |= daos.ContainerOpenFlagReadOnlyMetadata
	}

	var containerID string
	switch {
	case cmd.contLabel != "":
		containerID = cmd.contLabel
	case cmd.contUUID != uuid.Nil:
		containerID = cmd.contUUID.String()
	default:
		return errors.New("no container UUID or label supplied")
	}

	var err error
	var resp *api.ContainerOpenResp
	ctx := cmd.MustLogCtx()
	if cmd.pool != nil {
		resp, err = cmd.pool.OpenContainer(ctx, api.ContainerOpenReq{
			ID:    containerID,
			Flags: openFlags,
		})
	} else {
		resp, err = ContainerOpen(ctx, api.ContainerOpenReq{
			ID:      containerID,
			Flags:   openFlags,
			SysName: cmd.SysName,
			PoolID:  cmd.PoolID().String(),
		})
	}
	if err != nil {
		return err
	}
	cmd.container = resp.Connection

	// needed for compat with older code
	if err := cmd.container.FillHandle(unsafe.Pointer(&cmd.cContHandle)); err != nil {
		cmd.closeContainer()
		return err
	}

	return nil
}

func (cmd *containerBaseCmd) closeContainer() {
	if err := cmd.container.Close(cmd.MustLogCtx()); err != nil {
		cmd.Errorf("container close failed: %s", err)
	}
}

func (cmd *containerBaseCmd) connectPool(flags daos.PoolConnectFlag, ap *C.struct_cmd_args_s) (func(), error) {
	if err := cmd.poolBaseCmd.connectPool(flags); err != nil {
		return nil, err
	}

	if ap != nil {
		ap.pool = cmd.cPoolHandle
		if err := copyUUID(&ap.p_uuid, cmd.pool.UUID()); err != nil {
			cmd.disconnectPool()
			return nil, err
		}
	}

	return cmd.disconnectPool, nil
}

type containerCreateCmd struct {
	containerBaseCmd

	Type            ContTypeFlag         `long:"type" short:"t" description:"container type"`
	Path            string               `long:"path" short:"p" description:"container namespace path"`
	ChunkSize       ChunkSizeFlag        `long:"chunk-size" short:"z" description:"container chunk size"`
	ObjectClass     ObjClassFlag         `long:"oclass" short:"o" description:"default object class"`
	DirObjectClass  ObjClassFlag         `long:"dir-oclass" short:"d" description:"default directory object class"`
	FileObjectClass ObjClassFlag         `long:"file-oclass" short:"f" description:"default file object class"`
	CHints          string               `long:"hints" short:"H" description:"container hints"`
	Properties      CreatePropertiesFlag `long:"properties" description:"container properties"`
	Mode            ConsModeFlag         `long:"mode" short:"M" description:"DFS consistency mode"`
	ACLFile         string               `long:"acl-file" short:"A" description:"input file containing ACL"`
	Group           ui.ACLPrincipalFlag  `long:"group" short:"g" description:"group who will own the container (group@[domain])"`
	Attrs           ui.SetPropertiesFlag `long:"attrs" short:"a" description:"user-defined attributes (key:val[,key:val...])"`
	Args            struct {
		Label string `positional-arg-name:"label"`
	} `positional-args:"yes"`
}

func (cmd *containerCreateCmd) Execute(_ []string) (err error) {
	defer cmd.Properties.Cleanup()

	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	if cmd.Args.Label != "" {
		for key := range cmd.Properties.ParsedProps {
			if key == "label" {
				return errors.New("can't supply label arg and --properties label:")
			}
		}
		if err := cmd.Properties.AddPropVal("label", cmd.Args.Label); err != nil {
			return err
		}
		cmd.contLabel = cmd.Args.Label
	}

	if cmd.PoolID().Empty() {
		if cmd.Path == "" {
			return errors.New("no pool ID or dfs path supplied")
		}

		ap.path = C.CString(cmd.Path)
		rc := C.resolve_duns_pool(ap)
		freeString(ap.path)
		if err := daosError(rc); err != nil {
			return errors.Wrapf(err, "failed to resolve pool id from %q; use --pool <id>", filepath.Dir(cmd.Path))
		}

		pu, err := uuidFromC(ap.p_uuid)
		if err != nil {
			return err
		}
		cmd.poolBaseCmd.Args.Pool.UUID = pu
	}

	disconnectPool, err := cmd.connectPool(daos.PoolConnectFlagReadWrite, ap)
	if err != nil {
		return err
	}
	defer disconnectPool()

	var contID string
	contID, err = cmd.contCreate()
	if err != nil {
		return err
	}

	ci, err := cmd.pool.QueryContainer(cmd.MustLogCtx(), cmd.contUUID.String())
	if err != nil {
		if errors.Cause(err) != daos.NoPermission {
			return errors.Wrapf(err, "failed to query new container %s", contID)
		}

		// Special case for creating a container without permission to query it.
		cmd.Errorf("container %s was created, but query failed", contID)

		ci = new(daos.ContainerInfo)
		ci.PoolUUID = cmd.pool.UUID()
		ci.Type = cmd.Type.Type
		ci.ContainerUUID = cmd.contUUID
		ci.ContainerLabel = cmd.Args.Label
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(ci, nil)
	}

	var bld strings.Builder
	if err := pretty.PrintContainerInfo(&bld, ci, false); err != nil {
		return err
	}
	cmd.Info(bld.String())

	return nil
}

func (cmd *containerCreateCmd) contCreate() (string, error) {
	createProps, err := cmd.getCreateProps()
	if err != nil {
		return "", err
	}
	var cProps *C.daos_prop_t
	if createProps != nil {
		defer createProps.Free()
		cProps = (*C.daos_prop_t)(createProps.ToPtr()) // TODO: Remove with ContainerCreate API
	}

	var rc C.int
	var contUUID C.uuid_t
	if cmd.Type.Type == C.DAOS_PROP_CO_LAYOUT_POSIX {
		var attr C.dfs_attr_t

		// POSIX containers have extra attributes
		if cmd.ChunkSize.Set {
			attr.da_chunk_size = cmd.ChunkSize.Size
		}
		if cmd.ObjectClass.Set {
			attr.da_oclass_id = cmd.ObjectClass.Class
		}
		if cmd.DirObjectClass.Set {
			attr.da_dir_oclass_id = cmd.DirObjectClass.Class
		}
		if cmd.FileObjectClass.Set {
			attr.da_file_oclass_id = cmd.FileObjectClass.Class
		}
		if cmd.Mode.Set {
			attr.da_mode = cmd.Mode.Mode
		}
		if cmd.CHints != "" {
			hint := C.CString(cmd.CHints)
			defer freeString(hint)
			C.strncpy(&attr.da_hints[0], hint, C.DAOS_CONT_HINT_MAX_LEN-1)
		}
		attr.da_props = cProps

		dfsErrno := C.dfs_cont_create(cmd.cPoolHandle, &contUUID, &attr, nil, nil)
		rc = C.daos_errno2der(dfsErrno)
	} else {
		rc = C.daos_cont_create(cmd.cPoolHandle, &contUUID, cProps, nil)
	}
	if err := daosError(rc); err != nil {
		return "", errors.Wrap(err, "failed to create container")
	}

	cmd.contUUID, err = uuidFromC(contUUID)
	if err != nil {
		return "", err
	}
	contID := cmd.contUUID.String()
	cContID := C.CString(contID)
	defer freeString(cContID)

	cleanupContainer := func() {
		rc := C.daos_cont_destroy(cmd.cPoolHandle, cContID, goBool2int(true), nil)
		if err := daosError(rc); err != nil {
			cmd.Noticef("Failed to clean-up container %v", err)
		}
	}

	if len(cmd.Attrs.ParsedProps) != 0 {
		if err := cmd.openContainer(daos.ContainerOpenFlagReadWrite); err != nil {
			cleanupContainer()
			return "", errors.Wrapf(err, "failed to open new container %s", contID)
		}
		defer cmd.closeContainer()

		if err := setAttributes(cmd, cmd.container, contAttr, cmd.container.ID(), cmd.Attrs.ParsedProps); err != nil {
			cleanupContainer()
			return "", errors.Wrapf(err, "failed to set user attributes on new container %s", contID)
		}
	}

	if cmd.Path != "" {
		cPath := C.CString(cmd.Path)
		defer freeString(cPath)

		dunsErrno := C.duns_link_cont(cmd.cPoolHandle, cContID, cPath)
		rc := C.daos_errno2der(dunsErrno)
		if err := daosError(rc); err != nil {
			cleanupContainer()
			return "", errors.Wrapf(err, "duns_link_cont() failed")
		}
	}

	cmd.Infof("Successfully created container %s type %s", contID, cmd.Type.String())
	return contID, nil
}

func (cmd *containerCreateCmd) getCreateProps() (*daos.ContainerPropertyList, error) {
	var numEntries int
	if cmd.Properties.propList != nil {
		numEntries = cmd.Properties.propList.Count()
	}

	if cmd.ACLFile != "" {
		numEntries++
	}

	if cmd.Group != "" {
		numEntries++
	}

	hasType := func() bool {
		return cmd.Type.Set && cmd.Type.Type != C.DAOS_PROP_CO_LAYOUT_POSIX &&
			cmd.Type.Type != C.DAOS_PROP_CO_LAYOUT_UNKNOWN
	}
	if hasType() {
		numEntries++
	}

	if numEntries == 0 {
		return nil, nil
	}

	createPropList, err := daos.AllocateContainerPropertyList(numEntries)
	if err != nil {
		return nil, errors.Wrap(err, "failed to allocate container create property list")
	}

	if err := createPropList.CopyFrom(cmd.Properties.propList); err != nil {
		createPropList.Free()
		return nil, errors.Wrap(err, "failed to clone container create property list")
	}

	if cmd.ACLFile != "" {
		// The ACL becomes part of the daos_prop_t and will be freed with that structure
		cACL, _, err := aclFileToC(cmd.ACLFile)
		if err != nil {
			createPropList.Free()
			return nil, err
		}
		aclProp := createPropList.MustAddEntryByType(daos.ContainerPropACL)
		aclProp.SetValuePtr(unsafe.Pointer(cACL))
	}

	if cmd.Group != "" {
		// The group string becomes part of the daos_prop_t and will be freed with that structure
		grpProp := createPropList.MustAddEntryByType(daos.ContainerPropGroup)
		grpProp.SetString(cmd.Group.String())
	}

	if hasType() {
		typeProp := createPropList.MustAddEntryByType(daos.ContainerPropLayoutType)
		typeProp.SetValue(uint64(cmd.Type.Type))
	}

	return createPropList, nil
}

type existingContainerCmd struct {
	containerBaseCmd

	Path string `long:"path" short:"p" description:"unified namespace path"`
	Args struct {
		Container ContainerID `positional-arg-name:"container name or UUID" description:"required if --path is not used"`
	} `positional-args:"yes"`
}

func (cmd *existingContainerCmd) ContainerID() ui.LabelOrUUIDFlag {
	return cmd.Args.Container.LabelOrUUIDFlag
}

// parseContPathArgs is a gross hack to support the --path flag with positional
// arguments. It's necessary because the positional arguments are parsed at the
// same time as the flags, so we can't tell if the user is trying to use a path
// with positional arguments (e.g. --path=/tmp/dfuse set-attr foo:bar) until after
// the flags have been parsed. If --path was used, any consumed positional arguments
// are appended in reverse order to the end of the args slice so that the command
// handler (which must be aware of this convention) may parse them again. Otherwise,
// the positional arguments are parsed as normal pool/container IDs.
func (cmd *existingContainerCmd) parseContPathArgs(args []string) ([]string, error) {
	if contArg := cmd.Args.Container.String(); contArg != "" {
		if cmd.Path != "" {
			args = append(args, contArg)
			cmd.Args.Container.Clear()
		} else {
			if err := cmd.Args.Container.LabelOrUUIDFlag.UnmarshalFlag(contArg); err != nil {
				return nil, err
			}
		}
	}
	if poolArg := cmd.poolBaseCmd.Args.Pool.String(); poolArg != "" {
		if cmd.Path != "" {
			args = append(args, poolArg)
			cmd.poolBaseCmd.Args.Pool.Clear()
		} else {
			if err := cmd.poolBaseCmd.Args.Pool.LabelOrUUIDFlag.UnmarshalFlag(poolArg); err != nil {
				return nil, err
			}
		}
	}

	return args, nil
}

func (cmd *existingContainerCmd) resolveContainerPath(ap *C.struct_cmd_args_s) (err error) {
	if cmd.Path == "" {
		return errors.New("path cannot be empty")
	}
	if ap == nil {
		return errors.New("ap cannot be nil")
	}

	if err := resolveDunsPath(cmd.Path, ap); err != nil {
		return err
	}

	cmd.poolBaseCmd.Args.Pool.UUID, err = uuidFromC(ap.p_uuid)
	if err != nil {
		return
	}
	cmd.poolBaseCmd.poolUUID = cmd.poolBaseCmd.Args.Pool.UUID
	cmd.poolBaseCmd.Args.Pool.Label = C.GoString(&ap.pool_str[0])

	cmd.Args.Container.UUID, err = uuidFromC(ap.c_uuid)
	if err != nil {
		return
	}
	cmd.contUUID = cmd.Args.Container.UUID
	cmd.Args.Container.Label = C.GoString(&ap.cont_str[0])
	if cmd.Args.Container.Label != "" {
		cmd.contLabel = cmd.Args.Container.Label
	}
	cmd.Debugf("resolved %s into pool %s and container %s", cmd.Path, cmd.poolBaseCmd.Args.Pool, cmd.Args.Container)

	return
}

func (cmd *existingContainerCmd) resolveContainer(ap *C.struct_cmd_args_s) (err error) {
	if cmd.Path != "" {
		return cmd.resolveContainerPath(ap)
	}

	switch {
	case cmd.ContainerID().HasLabel():
		cmd.contLabel = cmd.ContainerID().Label
		if ap != nil {
			cLabel := C.CString(cmd.ContainerID().Label)
			defer freeString(cLabel)
			C.strncpy(&ap.cont_str[0], cLabel, C.DAOS_PROP_LABEL_MAX_LEN)
		}
	case cmd.ContainerID().HasUUID():
		cmd.contUUID = cmd.ContainerID().UUID
		if ap != nil {
			cUUIDstr := C.CString(cmd.contUUID.String())
			defer freeString(cUUIDstr)
			C.strncpy(&ap.cont_str[0], cUUIDstr, C.DAOS_PROP_LABEL_MAX_LEN)
		}
	default:
		return errors.New("no container label or UUID supplied")
	}

	cmd.Debugf("pool ID: %s, container ID: %s", cmd.PoolID(), cmd.ContainerID())

	return nil
}

func (cmd *existingContainerCmd) resolveAndOpen(contFlags daos.ContainerOpenFlag, ap *C.struct_cmd_args_s) (func(), error) {
	nulCleanFn := func() {}
	if err := cmd.resolveContainer(ap); err != nil {
		return nulCleanFn, err
	}

	if err := cmd.openContainer(contFlags); err != nil {
		return nulCleanFn, err
	}

	cleanup := cmd.closeContainer
	if ap != nil {
		if err := copyUUID(&ap.c_uuid, cmd.container.UUID()); err != nil {
			cleanup()
			return nulCleanFn, err
		}
		ap.cont = cmd.cContHandle
	}

	return cleanup, nil
}

type containerListCmd struct {
	poolBaseCmd
	Verbose bool `short:"v" long:"verbose" description:"Verbose output"`
}

func (cmd *containerListCmd) Execute(_ []string) error {
	cleanup, err := cmd.resolveAndConnect(daos.PoolConnectFlagReadOnly, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	contList, err := cmd.pool.ListContainers(cmd.MustLogCtx(), cmd.Verbose)
	if err != nil {
		return errors.Wrapf(err, "unable to list container for pool %s", cmd.PoolID())
	}

	if cmd.JSONOutputEnabled() {
		// Maintain compatibility with the JSON output from before.
		contIDs := make([]ContainerID, len(contList))
		for i, cont := range contList {
			contIDs[i].UUID = cont.ContainerUUID
			contIDs[i].Label = cont.ContainerLabel
		}
		return cmd.OutputJSON(contIDs, nil)
	}

	var bld strings.Builder
	pretty.PrintContainers(&bld, cmd.pool.ID(), contList, cmd.Verbose)
	cmd.Info(bld.String())

	return nil
}

type containerDestroyCmd struct {
	existingContainerCmd

	Force bool `long:"force" short:"f" description:"force the container destroy"`
}

func (cmd *containerDestroyCmd) Execute(_ []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	if err := cmd.resolveContainer(ap); err != nil {
		return err
	}

	if cmd.Path == "" {
		poolID := cmd.PoolID().String()
		contID := cmd.ContainerID().String()

		if contID == "" {
			return errors.New("no UUID or label or path for container")
		}
		if err := api.ContainerDestroy(cmd.MustLogCtx(), cmd.SysName, poolID, contID, cmd.Force); err != nil {
			return errors.Wrapf(err, "failed to destroy container %s", contID)
		}

		cmd.Infof("Successfully destroyed container %s", contID)
		return nil
	}

	// TODO: Add API for DUNS.
	var cleanup func()
	cleanup, err = cmd.connectPool(daos.PoolConnectFlagReadWrite, ap)
	if err != nil {
		cleanup, err = cmd.connectPool(daos.PoolConnectFlagReadOnly, ap)
		if err != nil {
			return err
		}
	}
	defer cleanup()

	cPath := C.CString(cmd.Path)
	defer freeString(cPath)
	if err := daosError(C.duns_destroy_path(cmd.cPoolHandle, cPath)); err != nil {
		return errors.Wrapf(err, "failed to destroy container %s", cmd.ContainerID())
	}

	cmd.Infof("Successfully destroyed container %s", cmd.Path)

	return nil
}

type containerListObjectsCmd struct {
	existingContainerCmd

	Epoch uint64 `long:"epc" short:"e" description:"container epoch"`
}

func (cmd *containerListObjectsCmd) Execute(_ []string) error {
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

	if cmd.Epoch > 0 {
		ap.epc = C.uint64_t(cmd.Epoch)
	}

	snapOpts := uint32(C.DAOS_SNAP_OPT_CR | C.DAOS_SNAP_OPT_OIT)
	rc := C.daos_cont_create_snap_opt(ap.cont, &ap.epc, nil, snapOpts, nil)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err, "failed to create snapshot for container %s", cmd.ContainerID())
	}
	defer func() {
		var epr C.daos_epoch_range_t
		epr.epr_lo = ap.epc
		epr.epr_hi = ap.epc
		if err := daosError(C.daos_cont_destroy_snap(ap.cont, epr, nil)); err != nil {
			cmd.Errorf("failed to destroy snapshot in cleanup: %v", err)
		}
	}()

	oit := C.daos_handle_t{}
	rc = C.daos_oit_open(ap.cont, ap.epc, &oit, nil)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err, "failed to open OIT for container %s", cmd.ContainerID())
	}
	defer func() {
		rc = C.daos_cont_snap_oit_destroy(ap.cont, oit, nil)
		if err := daosError(rc); err != nil {
			cmd.Errorf("failed to destroy OIT in cleanup: %v", err)
		}
		rc = C.daos_oit_close(oit, nil)
		if err := daosError(rc); err != nil {
			cmd.Errorf("failed to close OIT in cleanup: %v", err)
		}
	}()

	// NB: It is somewhat inefficient to build up a slice of OID strings for
	// the JSON output format, but it is simple. If it turns out to be a problem,
	// we can implement a custom JSON output handler that streams the OID
	// strings directly to output.
	var oids []string
	var readOids C.uint32_t
	oidArr := [C.OID_ARR_SIZE]C.daos_obj_id_t{}
	anchor := C.daos_anchor_t{}
	for {
		if C.daos_anchor_is_eof(&anchor) {
			break
		}

		readOids = C.OID_ARR_SIZE
		rc = C.daos_oit_list(oit, &oidArr[0], &readOids, &anchor, nil)
		if err := daosError(rc); err != nil {
			return errors.Wrapf(err, "failed to list objects for container %s", cmd.ContainerID())
		}

		for i := C.uint32_t(0); i < readOids; i++ {
			oid := fmt.Sprintf("%d.%d", oidArr[i].hi, oidArr[i].lo)

			if !cmd.JSONOutputEnabled() {
				cmd.Infof("%s", oid)
				continue
			}
			oids = append(oids, oid)
		}
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(oids, nil)
	}

	return nil
}

type containerStatCmd struct {
	existingContainerCmd
}

func (cmd *containerStatCmd) Execute(_ []string) error {
	return nil
}

type containerQueryCmd struct {
	existingContainerCmd
}

func (cmd *containerQueryCmd) Execute(_ []string) error {
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

	ci, err := cmd.container.Query(cmd.MustLogCtx())
	if err != nil {
		return errors.Wrapf(err, "failed to query container %s", cmd.ContainerID())
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(ci, nil)
	}

	var bld strings.Builder
	if err := pretty.PrintContainerInfo(&bld, ci, true); err != nil {
		return err
	}
	cmd.Info(bld.String())

	return nil
}

type containerCloneCmd struct {
	daosCmd

	Source      string `long:"src" short:"S" description:"source container" required:"1"`
	Destination string `long:"dst" short:"D" description:"destination container" required:"1"`
}

func (cmd *containerCloneCmd) Execute(_ []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	disconnect, err := cmd.initDAOS()
	if err != nil {
		return err
	}
	defer disconnect()

	ap.src = C.CString(cmd.Source)
	defer freeString(ap.src)
	ap.dst = C.CString(cmd.Destination)
	defer freeString(ap.dst)

	ap.c_op = C.CONT_CLONE
	rc := C.cont_clone_hdlr(ap)

	if err := daosError(rc); err != nil {
		return errors.Wrapf(err, "failed to clone container %s", cmd.Source)
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(struct {
			SourcePool string `json:"src_pool"`
			SourceCont string `json:"src_cont"`
			DestPool   string `json:"dst_pool"`
			DestCont   string `json:"dst_cont"`
		}{
			C.GoString(&ap.dm_args.src_pool[0]),
			C.GoString(&ap.dm_args.src_cont[0]),
			C.GoString(&ap.dm_args.dst_pool[0]),
			C.GoString(&ap.dm_args.dst_cont[0]),
		}, nil)
	}

	// Compat with old-style output
	cmd.Infof("Successfully copied to destination container %s", C.GoString(&ap.dm_args.dst_cont[0]))

	return nil
}

type containerCheckCmd struct {
	existingContainerCmd

	Epoch uint64 `long:"epc" short:"e" description:"container epoch"`
}

func (cmd *containerCheckCmd) Execute(_ []string) error {
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

	if cmd.Epoch > 0 {
		ap.epc = C.uint64_t(cmd.Epoch)
	}

	rc := C.cont_check_hdlr(ap)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err,
			"failed to check container %s",
			cmd.ContainerID())
	}

	return nil
}

type containerListAttrsCmd struct {
	existingContainerCmd

	Verbose bool `long:"verbose" short:"V" description:"Include values"`
}

func (cmd *containerListAttrsCmd) Execute(args []string) error {
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

	return listAttributes(cmd, cmd.container, contAttr, cmd.container.ID(), cmd.Verbose)
}

type containerDelAttrCmd struct {
	existingContainerCmd

	FlagAttr string `long:"attr" short:"a" description:"attribute name (deprecated; use positional argument)"`
	Args     struct {
		Attrs ui.GetPropertiesFlag `positional-arg-name:"key[,key...]"`
	} `positional-args:"yes"`
}

func (cmd *containerDelAttrCmd) Execute(args []string) error {
	if cmd.FlagAttr != "" {
		if len(cmd.Args.Attrs.ParsedProps) > 0 {
			return errors.New("cannot specify both --attr and positional arguments")
		}
		cmd.Args.Attrs.ParsedProps = make(common.StringSet)
		cmd.Args.Attrs.ParsedProps.Add(cmd.FlagAttr)
		cmd.FlagAttr = ""
	}
	if len(cmd.Args.Attrs.ParsedProps) == 0 {
		return errors.New("attribute name is required")
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

	return delAttributes(cmd, cmd.container, contAttr, cmd.container.ID(), cmd.Args.Attrs.ParsedProps.ToSlice()...)
}

type containerGetAttrCmd struct {
	existingContainerCmd

	FlagAttr string `long:"attr" short:"a" description:"single attribute name (deprecated; use positional argument)"`
	Args     struct {
		Attrs ui.GetPropertiesFlag `positional-arg-name:"key[,key...]"`
	} `positional-args:"yes"`
}

func (cmd *containerGetAttrCmd) Execute(args []string) error {
	if cmd.FlagAttr != "" {
		if len(cmd.Args.Attrs.ParsedProps) > 0 {
			return errors.New("cannot specify both --attr and positional arguments")
		}
		cmd.Args.Attrs.ParsedProps = make(common.StringSet)
		cmd.Args.Attrs.ParsedProps.Add(cmd.FlagAttr)
		cmd.FlagAttr = ""
	} else if len(args) > 0 {
		if err := cmd.Args.Attrs.UnmarshalFlag(args[len(args)-1]); err != nil {
			return err
		}
	}

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

	return getAttributes(cmd, cmd.container, contAttr, cmd.container.ID(), cmd.Args.Attrs.ParsedProps.ToSlice()...)
}

type containerSetAttrCmd struct {
	existingContainerCmd

	FlagAttr  string `long:"attr" short:"a" description:"attribute name (deprecated; use positional argument)"`
	FlagValue string `long:"value" short:"v" description:"attribute value (deprecated; use positional argument)"`
	Args      struct {
		Attrs ui.SetPropertiesFlag `positional-arg-name:"key:val[,key:val...]"`
	} `positional-args:"yes"`
}

func (cmd *containerSetAttrCmd) Execute(args []string) error {
	if cmd.FlagAttr != "" || cmd.FlagValue != "" {
		if cmd.FlagAttr == "" || cmd.FlagValue == "" {
			return errors.New("both --attr and --value are required if either are used")
		}
		if len(cmd.Args.Attrs.ParsedProps) > 0 {
			return errors.New("cannot specify both --attr and positional arguments")
		}
		cmd.Args.Attrs.ParsedProps = make(map[string]string)
		cmd.Args.Attrs.ParsedProps[cmd.FlagAttr] = cmd.FlagValue
		cmd.FlagAttr = ""
		cmd.FlagValue = ""
	} else if len(args) > 0 {
		if err := cmd.Args.Attrs.UnmarshalFlag(args[len(args)-1]); err != nil {
			return err
		}
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

	return setAttributes(cmd, cmd.container, contAttr, cmd.container.ID(), cmd.Args.Attrs.ParsedProps)
}

type containerGetPropCmd struct {
	existingContainerCmd

	PropsFlag GetPropertiesFlag `long:"properties" description:"container properties to get (deprecated: use positional argument)"`
	Args      struct {
		Props GetPropertiesFlag `positional-arg-name:"key[,key...]"`
	} `positional-args:"yes"`
}

func (cmd *containerGetPropCmd) Execute(args []string) error {
	if len(args) > 0 {
		if err := cmd.Args.Props.UnmarshalFlag(args[len(args)-1]); err != nil {
			return err
		}
	}
	if len(cmd.PropsFlag.names) > 0 {
		if len(cmd.Args.Props.names) > 0 {
			return errors.New("cannot specify both --properties and positional arguments")
		}
		cmd.Args.Props = cmd.PropsFlag
	}
	defer cmd.Args.Props.Cleanup()

	if len(cmd.Args.Props.names) == 0 {
		// Ensure that things are set up correctly for the default case.
		if err := cmd.Args.Props.UnmarshalFlag(""); err != nil {
			return err
		}
	}

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

	propList, err := cmd.container.GetProperties(cmd.MustLogCtx(), cmd.Args.Props.names...)
	if err != nil {
		return errors.Wrapf(err, "failed to fetch properties for container %s", cmd.ContainerID())
	}
	defer propList.Free()

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(propList.Properties(), nil)
	}

	title := fmt.Sprintf("Properties for container %s", cmd.ContainerID())
	var bld strings.Builder
	pretty.PrintContainerProperties(&bld, title, propList.Properties()...)

	cmd.Info(bld.String())

	return nil
}

type containerSetPropCmd struct {
	existingContainerCmd

	PropsFlag SetPropertiesFlag `long:"properties" description:"container properties to set (deprecated: use positional argument)"`
	Args      struct {
		Props SetPropertiesFlag `positional-arg-name:"key:val[,key:val...]"`
	} `positional-args:"yes"`
}

func (cmd *containerSetPropCmd) Execute(args []string) error {
	if len(args) > 0 {
		if err := cmd.Args.Props.UnmarshalFlag(args[len(args)-1]); err != nil {
			return err
		}
	}
	if len(cmd.PropsFlag.ParsedProps) > 0 {
		if len(cmd.Args.Props.ParsedProps) > 0 {
			return errors.New("cannot specify both --properties and positional arguments")
		}
		cmd.Args.Props = cmd.PropsFlag
	}
	defer cmd.Args.Props.Cleanup()

	if len(cmd.Args.Props.ParsedProps) == 0 {
		return errors.New("property name and value are required")
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

	if err := cmd.container.SetProperties(cmd.MustLogCtx(), cmd.Args.Props.propList); err != nil {
		return errors.Wrapf(err, "failed to set properties on container %s", cmd.ContainerID())
	}

	cmd.Info("Properties were successfully set")
	return nil
}

// Experiment below with container ID completion.

type cmplCmd struct {
	cliOptions

	// Consume these to leave the pool id as the only
	// positional argument.
	Args struct {
		Object  string
		Command string
	} `positional-args:"yes"`
}

type poolFlagCmd struct {
	cmplCmd
	poolBaseCmd
}

func parsePoolFlag() *poolFlagCmd {
	// Avoid recursion when doing completion resolution.
	gfcVal, gfcActive := os.LookupEnv("GO_FLAGS_COMPLETION")
	os.Unsetenv("GO_FLAGS_COMPLETION")
	defer func() {
		if gfcActive {
			os.Setenv("GO_FLAGS_COMPLETION", gfcVal)
		}
	}()

	var pfc poolFlagCmd
	parser := flags.NewParser(&pfc, flags.HelpFlag|flags.PassDoubleDash|flags.IgnoreUnknown)
	parser.ParseArgs(os.Args[1:])

	return &pfc
}

type ContainerID struct {
	argOrID
}

// Implement the completion handler to provide a list of container IDs
// as completion items.
func (f *ContainerID) Complete(match string) (comps []flags.Completion) {
	pf := parsePoolFlag()
	pf.Logger = &logging.LeveledLogger{}

	fini, err := pf.initDAOS()
	if err != nil {
		return
	}
	defer fini()

	cleanup, err := pf.resolveAndConnect(daos.PoolConnectFlagReadOnly, nil)
	if err != nil {
		return
	}
	defer cleanup()

	contList, err := pf.pool.ListContainers(pf.MustLogCtx(), false)
	if err != nil {
		return
	}

	for _, cont := range contList {
		var contId string
		if strings.HasPrefix(cont.ContainerLabel, match) {
			contId = cont.ContainerLabel
		} else if strings.HasPrefix(cont.ContainerUUID.String(), match) {
			contId = cont.ContainerUUID.String()
		}

		if contId != "" {
			comps = append(comps, flags.Completion{
				Item: cont.ContainerLabel,
			})
		}
	}

	return
}

type containerLinkCmd struct {
	containerBaseCmd

	Path string `long:"path" short:"p" description:"container namespace path to create"`
	Args struct {
		Container ContainerID `positional-arg-name:"container name or UUID" description:"Container to link in the namespace"`
	} `positional-args:"yes"`
}

func (cmd *containerLinkCmd) ContainerID() ui.LabelOrUUIDFlag {
	return cmd.Args.Container.LabelOrUUIDFlag
}

func (cmd *containerLinkCmd) Execute(_ []string) (err error) {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	var cleanup func()
	cleanup, err = cmd.connectPool(C.DAOS_COO_RO, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	cmd.Debugf("Creating a namespace link %s for container %s", cmd.Path, cmd.ContainerID())

	cPath := C.CString(cmd.Path)
	defer freeString(cPath)

	var rc C.int
	switch {
	case cmd.ContainerID().HasUUID():
		cmd.contUUID = cmd.ContainerID().UUID
		cUUIDstr := C.CString(cmd.contUUID.String())
		defer freeString(cUUIDstr)
		rc = C.duns_link_cont(cmd.cPoolHandle, cUUIDstr, cPath)
	case cmd.ContainerID().Label != "":
		cLabel := C.CString(cmd.ContainerID().Label)
		defer freeString(cLabel)
		rc = C.duns_link_cont(cmd.cPoolHandle, cLabel, cPath)
	default:
		return errors.New("no UUID or label for container")
	}

	if err := daosError(rc); err != nil {
		return errors.Wrapf(err,
			"failed to link container %s in the namespace",
			cmd.ContainerID())
	}

	return nil
}

type containerEvictCmd struct {
	existingContainerCmd

	All bool `long:"all" short:"a" description:"evict all handles from all users"`
}

func (cmd *containerEvictCmd) Execute(_ []string) (err error) {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	var co_flags daos.ContainerOpenFlag
	if cmd.All {
		co_flags = daos.ContainerOpenFlagEvictAll | daos.ContainerOpenFlagExclusive
	} else {
		co_flags = daos.ContainerOpenFlagEvict | daos.ContainerOpenFlagReadOnly
	}

	cleanup, err := cmd.resolveAndOpen(co_flags, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	return nil
}
