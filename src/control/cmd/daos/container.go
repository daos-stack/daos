//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"unsafe"

	"github.com/dustin/go-humanize"
	"github.com/google/uuid"
	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
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
	contUUID  uuid.UUID
	contLabel string

	cContHandle C.daos_handle_t
}

func (cmd *containerBaseCmd) contUUIDPtr() *C.uchar {
	if cmd.contUUID == uuid.Nil {
		cmd.Error("contUUIDPtr(): nil UUID")
		return nil
	}
	return (*C.uchar)(unsafe.Pointer(&cmd.contUUID[0]))
}

func (cmd *containerBaseCmd) openContainer(openFlags C.uint) error {
	openFlags |= C.DAOS_COO_FORCE
	if (openFlags & C.DAOS_COO_RO) != 0 {
		openFlags |= C.DAOS_COO_RO_MDSTATS
	}

	var rc C.int
	switch {
	case cmd.contLabel != "":
		var contInfo C.daos_cont_info_t
		cLabel := C.CString(cmd.contLabel)
		defer freeString(cLabel)

		cmd.Debugf("opening container: %s", cmd.contLabel)
		rc = C.daos_cont_open(cmd.cPoolHandle, cLabel, openFlags,
			&cmd.cContHandle, &contInfo, nil)
		if rc == 0 {
			var err error
			cmd.contUUID, err = uuidFromC(contInfo.ci_uuid)
			if err != nil {
				cmd.closeContainer()
				return err
			}
		}
	case cmd.contUUID != uuid.Nil:
		cmd.Debugf("opening container: %s", cmd.contUUID)
		cUUIDstr := C.CString(cmd.contUUID.String())
		defer freeString(cUUIDstr)
		rc = C.daos_cont_open(cmd.cPoolHandle, cUUIDstr,
			openFlags, &cmd.cContHandle, nil, nil)
	default:
		return errors.New("no container UUID or label supplied")
	}

	return daosError(rc)
}

func (cmd *containerBaseCmd) closeContainer() {
	cmd.Debugf("closing container: %s", cmd.contUUID)
	// Hack for NLT fault injection testing: If the rc
	// is -DER_NOMEM, retry once in order to actually
	// shut down and release resources.
	rc := C.daos_cont_close(cmd.cContHandle, nil)
	if rc == -C.DER_NOMEM {
		rc = C.daos_cont_close(cmd.cContHandle, nil)
	}

	if err := daosError(rc); err != nil {
		cmd.Errorf("container close failed: %s", err)
	}
}

func (cmd *containerBaseCmd) queryContainer() (*containerInfo, error) {
	ci := newContainerInfo(&cmd.poolUUID, &cmd.contUUID)
	var cType [10]C.char

	props, entries, err := allocProps(3)
	if err != nil {
		return nil, err
	}
	entries[0].dpe_type = C.DAOS_PROP_CO_LAYOUT_TYPE
	props.dpp_nr++
	entries[1].dpe_type = C.DAOS_PROP_CO_LABEL
	props.dpp_nr++
	entries[2].dpe_type = C.DAOS_PROP_CO_REDUN_FAC
	props.dpp_nr++
	defer func() { C.daos_prop_free(props) }()

	rc := C.daos_cont_query(cmd.cContHandle, &ci.dci, props, nil)
	if err := daosError(rc); err != nil {
		return nil, err
	}

	lType := C.get_dpe_val(&entries[0])
	C.daos_unparse_ctype(C.ushort(lType), &cType[0])
	ci.Type = C.GoString(&cType[0])

	if C.get_dpe_str(&entries[1]) == nil {
		ci.ContainerLabel = ""
	} else {
		cStr := C.get_dpe_str(&entries[1])
		ci.ContainerLabel = C.GoString(cStr)
	}

	ci.RedundancyFactor = uint32(C.get_dpe_val(&entries[2]))

	if lType == C.DAOS_PROP_CO_LAYOUT_POSIX {
		var dfs *C.dfs_t
		var attr C.dfs_attr_t
		var oclass [C.MAX_OBJ_CLASS_NAME_LEN]C.char
		var dir_oclass [C.MAX_OBJ_CLASS_NAME_LEN]C.char
		var file_oclass [C.MAX_OBJ_CLASS_NAME_LEN]C.char

		rc := C.dfs_mount(cmd.cPoolHandle, cmd.cContHandle, C.O_RDONLY, &dfs)
		if err := dfsError(rc); err != nil {
			return nil, errors.Wrap(err, "failed to mount container")
		}

		rc = C.dfs_query(dfs, &attr)
		if err := dfsError(rc); err != nil {
			return nil, errors.Wrap(err, "failed to query container")
		}
		C.daos_oclass_id2name(attr.da_oclass_id, &oclass[0])
		ci.ObjectClass = C.GoString(&oclass[0])
		C.daos_oclass_id2name(attr.da_dir_oclass_id, &dir_oclass[0])
		ci.DirObjectClass = C.GoString(&dir_oclass[0])
		C.daos_oclass_id2name(attr.da_file_oclass_id, &file_oclass[0])
		ci.FileObjectClass = C.GoString(&file_oclass[0])
		ci.CHints = C.GoString(&attr.da_hints[0])
		ci.ChunkSize = uint64(attr.da_chunk_size)

		if err := dfsError(C.dfs_umount(dfs)); err != nil {
			return nil, errors.Wrap(err, "failed to unmount container")
		}
	}

	return ci, nil
}

func (cmd *containerBaseCmd) connectPool(flags C.uint, ap *C.struct_cmd_args_s) (func(), error) {
	if err := cmd.poolBaseCmd.connectPool(flags); err != nil {
		return nil, err
	}

	if ap != nil {
		ap.pool = cmd.cPoolHandle
		if err := copyUUID(&ap.p_uuid, cmd.poolUUID); err != nil {
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
	Args            struct {
		Label string `positional-arg-name:"label"`
	} `positional-args:"yes"`
}

func (cmd *containerCreateCmd) Execute(_ []string) (err error) {
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

	if cmd.Properties.props != nil {
		ap.props = cmd.Properties.props
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

	disconnectPool, err := cmd.connectPool(C.DAOS_PC_RW, ap)
	if err != nil {
		return err
	}
	defer disconnectPool()

	var contID string
	if cmd.Path != "" {
		contID, err = cmd.contCreateUNS()
	} else {
		contID, err = cmd.contCreate()
	}
	if err != nil {
		return err
	}

	if err := cmd.openContainer(C.DAOS_COO_RO); err != nil {
		return errors.Wrapf(err, "failed to open new container %s", contID)
	}
	defer cmd.closeContainer()

	var ci *containerInfo
	ci, err = cmd.queryContainer()
	if err != nil {
		if errors.Cause(err) != daos.NoPermission {
			return errors.Wrapf(err, "failed to query new container %s", contID)
		}

		// Special case for creating a container without permission to query it.
		cmd.Errorf("container %s was created, but query failed", contID)

		ci = new(containerInfo)
		ci.PoolUUID = &cmd.poolUUID
		ci.Type = cmd.Type.String()
		ci.ContainerUUID = &cmd.contUUID
		ci.ContainerLabel = cmd.Args.Label
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(ci, nil)
	}

	var bld strings.Builder
	if err := printContainerInfo(&bld, ci, false); err != nil {
		return err
	}
	cmd.Info(bld.String())

	return nil
}

func (cmd *containerCreateCmd) contCreate() (string, error) {
	props, cleanupProps, err := cmd.getCreateProps()
	if err != nil {
		return "", err
	}
	defer cleanupProps()

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
		attr.da_props = props

		dfsErrno := C.dfs_cont_create(cmd.cPoolHandle, &contUUID, &attr, nil, nil)
		rc = C.daos_errno2der(dfsErrno)
	} else {
		rc = C.daos_cont_create(cmd.cPoolHandle, &contUUID, props, nil)
	}
	if err := daosError(rc); err != nil {
		return "", errors.Wrap(err, "failed to create container")
	}

	cmd.contUUID, err = uuidFromC(contUUID)
	if err != nil {
		return "", err
	}

	var contID string
	if cmd.contUUID == uuid.Nil {
		contID = cmd.contLabel
	} else {
		contID = cmd.contUUID.String()
	}

	cmd.Infof("Successfully created container %s", contID)
	return contID, nil
}

func (cmd *containerCreateCmd) contCreateUNS() (string, error) {
	var dattr C.struct_duns_attr_t

	props, cleanupProps, err := cmd.getCreateProps()
	if err != nil {
		return "", err
	}
	defer cleanupProps()
	dattr.da_props = props

	if !cmd.Type.Set {
		return "", errors.New("container type is required for UNS")
	}
	dattr.da_type = cmd.Type.Type

	if cmd.poolUUID != uuid.Nil {
		poolUUIDStr := C.CString(cmd.poolUUID.String())
		defer freeString(poolUUIDStr)
		C.uuid_parse(poolUUIDStr, &dattr.da_puuid[0])
	}
	if cmd.contUUID != uuid.Nil {
		contUUIDStr := C.CString(cmd.contUUID.String())
		defer freeString(contUUIDStr)
		C.uuid_parse(contUUIDStr, &dattr.da_cuuid[0])
	}

	if cmd.ChunkSize.Set {
		dattr.da_chunk_size = cmd.ChunkSize.Size
	}
	if cmd.ObjectClass.Set {
		dattr.da_oclass_id = cmd.ObjectClass.Class
	}
	if cmd.DirObjectClass.Set {
		dattr.da_dir_oclass_id = cmd.DirObjectClass.Class
	}
	if cmd.FileObjectClass.Set {
		dattr.da_file_oclass_id = cmd.FileObjectClass.Class
	}
	if cmd.CHints != "" {
		hint := C.CString(cmd.CHints)
		defer freeString(hint)
		C.strncpy(&dattr.da_hints[0], hint, C.DAOS_CONT_HINT_MAX_LEN-1)
	}

	cPath := C.CString(cmd.Path)
	defer freeString(cPath)

	dunsErrno := C.duns_create_path(cmd.cPoolHandle, cPath, &dattr)
	rc := C.daos_errno2der(dunsErrno)
	if err := daosError(rc); err != nil {
		return "", errors.Wrapf(err, "duns_create_path() failed")
	}

	contID := C.GoString(&dattr.da_cont[0])
	cmd.contUUID, err = uuid.Parse(contID)
	if err != nil {
		cmd.contLabel = contID
	}
	cmd.Infof("Successfully created container %s type %s", contID, cmd.Type.String())
	return contID, nil
}

func (cmd *containerCreateCmd) getCreateProps() (*C.daos_prop_t, func(), error) {
	var numEntries int
	if cmd.Properties.props != nil {
		numEntries = int(cmd.Properties.props.dpp_nr)
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
		return nil, func() {}, nil
	}

	props, entries, err := allocProps(numEntries)
	if err != nil {
		return nil, nil, err
	}

	var propIdx int
	if cmd.Properties.props != nil {
		for _, oldEntry := range unsafe.Slice(cmd.Properties.props.dpp_entries, cmd.Properties.props.dpp_nr) {
			rc := C.daos_prop_entry_copy(&oldEntry, &entries[propIdx])
			if err := daosError(rc); err != nil {
				C.daos_prop_free(props)
				return nil, nil, errors.Wrap(err, "failed to copy container properties")
			}
			propIdx++
		}
	}

	if cmd.ACLFile != "" {
		// The ACL becomes part of the daos_prop_t and will be freed with that structure
		cACL, _, err := aclFileToC(cmd.ACLFile)
		if err != nil {
			C.daos_prop_free(props)
			return nil, nil, err
		}
		entries[propIdx].dpe_type = C.DAOS_PROP_CO_ACL
		C.set_dpe_val_ptr(&entries[propIdx], unsafe.Pointer(cACL))
		propIdx++
	}

	if cmd.Group != "" {
		// The group string becomes part of the daos_prop_t and will be freed with that structure
		entries[propIdx].dpe_type = C.DAOS_PROP_CO_OWNER_GROUP
		C.set_dpe_str(&entries[propIdx], C.CString(cmd.Group.String()))
		propIdx++
	}

	if hasType() {
		entries[propIdx].dpe_type = C.DAOS_PROP_CO_LAYOUT_TYPE
		C.set_dpe_val(&entries[propIdx], C.ulong(cmd.Type.Type))
		propIdx++
	}
	props.dpp_nr = C.uint32_t(numEntries)
	return props, func() { C.daos_prop_free(props) }, nil
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

func (cmd *existingContainerCmd) resolveAndConnect(contFlags C.uint, ap *C.struct_cmd_args_s) (cleanFn func(), err error) {
	if err = cmd.resolveContainer(ap); err != nil {
		return
	}

	var cleanupPool func()
	cleanupPool, err = cmd.connectPool(C.DAOS_PC_RO, ap)
	if err != nil {
		return
	}

	if err = cmd.openContainer(contFlags); err != nil {
		cleanupPool()
		return
	}

	if ap != nil {
		if err = copyUUID(&ap.c_uuid, cmd.contUUID); err != nil {
			cleanupPool()
			return
		}
		ap.cont = cmd.cContHandle
	}

	return func() {
		cmd.closeContainer()
		cleanupPool()
	}, nil
}

func (cmd *existingContainerCmd) getAttr(name string) (*attribute, error) {
	return getDaosAttribute(cmd.cContHandle, contAttr, name)
}

type containerListCmd struct {
	poolBaseCmd
}

func listContainers(hdl C.daos_handle_t) ([]*ContainerID, error) {
	extra_cont_margin := C.size_t(16)

	// First call gets the current number of containers.
	var ncont C.daos_size_t
	rc := C.daos_pool_list_cont(hdl, &ncont, nil, nil)
	if err := daosError(rc); err != nil {
		return nil, errors.Wrap(err, "pool list containers failed")
	}

	// No containers.
	if ncont == 0 {
		return nil, nil
	}

	var cConts *C.struct_daos_pool_cont_info
	// Extend ncont with a safety margin to account for containers
	// that might have been created since the first API call.
	ncont += extra_cont_margin
	cConts = (*C.struct_daos_pool_cont_info)(C.calloc(C.sizeof_struct_daos_pool_cont_info, ncont))
	if cConts == nil {
		return nil, errors.New("calloc() for containers failed")
	}
	dpciSlice := (*[1 << 30]C.struct_daos_pool_cont_info)(
		unsafe.Pointer(cConts))[:ncont:ncont]
	cleanup := func() {
		C.free(unsafe.Pointer(cConts))
	}

	rc = C.daos_pool_list_cont(hdl, &ncont, cConts, nil)
	if err := daosError(rc); err != nil {
		cleanup()
		return nil, err
	}

	out := make([]*ContainerID, ncont)
	for i := range out {
		out[i] = new(ContainerID)
		out[i].UUID = uuid.Must(uuidFromC(dpciSlice[i].pci_uuid))
		out[i].Label = C.GoString(&dpciSlice[i].pci_label[0])
	}

	C.free(unsafe.Pointer(cConts))

	return out, nil
}

func printContainers(out io.Writer, contIDs []*ContainerID) {
	if len(contIDs) == 0 {
		fmt.Fprintf(out, "No containers.\n")
		return
	}

	uuidTitle := "UUID"
	labelTitle := "Label"
	titles := []string{uuidTitle, labelTitle}

	table := []txtfmt.TableRow{}
	for _, id := range contIDs {
		table = append(table,
			txtfmt.TableRow{
				uuidTitle:  id.UUID.String(),
				labelTitle: id.Label,
			})
	}

	tf := txtfmt.NewTableFormatter(titles...)
	tf.InitWriter(out)
	tf.Format(table)
}

func (cmd *containerListCmd) Execute(_ []string) error {
	cleanup, err := cmd.resolveAndConnect(C.DAOS_PC_RO, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	contIDs, err := listContainers(cmd.cPoolHandle)
	if err != nil {
		return errors.Wrapf(err,
			"unable to list containers for pool %s", cmd.PoolID())
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(contIDs, nil)
	}

	var bld strings.Builder
	printContainers(&bld, contIDs)
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

	var cleanup func()
	cleanup, err = cmd.connectPool(C.DAOS_COO_RW, ap)
	if err != nil {
		// Even if we don't have pool-level write permissions, we may
		// have delete permissions at the container level.
		cleanup, err = cmd.connectPool(C.DAOS_COO_RO, ap)
		if err != nil {
			return err
		}
	}
	defer cleanup()

	cmd.Debugf("destroying container %s (force: %t)",
		cmd.ContainerID(), cmd.Force)

	var rc C.int
	switch {
	case cmd.Path != "":
		cPath := C.CString(cmd.Path)
		defer freeString(cPath)
		rc = C.duns_destroy_path(cmd.cPoolHandle, cPath)
	case cmd.ContainerID().HasUUID():
		cUUIDstr := C.CString(cmd.contUUID.String())
		defer freeString(cUUIDstr)
		rc = C.daos_cont_destroy(cmd.cPoolHandle, cUUIDstr,
			goBool2int(cmd.Force), nil)
	case cmd.ContainerID().Label != "":
		cLabel := C.CString(cmd.ContainerID().Label)
		defer freeString(cLabel)
		rc = C.daos_cont_destroy(cmd.cPoolHandle,
			cLabel, goBool2int(cmd.Force), nil)
	default:
		return errors.New("no UUID or label or path for container")
	}

	if err := daosError(rc); err != nil {
		return errors.Wrapf(err,
			"failed to destroy container %s",
			cmd.ContainerID())
	}

	if cmd.ContainerID().Empty() {
		cmd.Infof("Successfully destroyed container %s", cmd.Path)
	} else {
		cmd.Infof("Successfully destroyed container %s", cmd.ContainerID())
	}

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

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RW, ap)
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

func printContainerInfo(out io.Writer, ci *containerInfo, verbose bool) error {
	rows := []txtfmt.TableRow{
		{"Container UUID": ci.ContainerUUID.String()},
	}
	if ci.ContainerLabel != "" {
		rows = append(rows, txtfmt.TableRow{"Container Label": ci.ContainerLabel})
	}
	rows = append(rows, txtfmt.TableRow{"Container Type": ci.Type})

	if verbose {
		rows = append(rows, []txtfmt.TableRow{
			{"Pool UUID": ci.PoolUUID.String()},
			{"Container redundancy factor": fmt.Sprintf("%d", ci.RedundancyFactor)},
			{"Number of open handles": fmt.Sprintf("%d", *ci.NumHandles)},
			{"Latest open time": fmt.Sprintf("%#x (%s)", *ci.OpenTime, daos.HLC(*ci.OpenTime))},
			{"Latest close/modify time": fmt.Sprintf("%#x (%s)", *ci.CloseModifyTime, daos.HLC(*ci.CloseModifyTime))},
			{"Number of snapshots": fmt.Sprintf("%d", *ci.NumSnapshots)},
		}...)

		if *ci.LatestSnapshot != 0 {
			rows = append(rows, txtfmt.TableRow{"Latest Persistent Snapshot": fmt.Sprintf("%#x (%s)", *ci.LatestSnapshot, daos.HLC(*ci.LatestSnapshot))})
		}
		if ci.ObjectClass != "" {
			rows = append(rows, txtfmt.TableRow{"Object Class": ci.ObjectClass})
		}
		if ci.DirObjectClass != "" {
			rows = append(rows, txtfmt.TableRow{"Dir Object Class": ci.DirObjectClass})
		}
		if ci.FileObjectClass != "" {
			rows = append(rows, txtfmt.TableRow{"File Object Class": ci.FileObjectClass})
		}
		if ci.CHints != "" {
			rows = append(rows, txtfmt.TableRow{"Hints": ci.CHints})
		}
		if ci.ChunkSize > 0 {
			rows = append(rows, txtfmt.TableRow{"Chunk Size": humanize.IBytes(ci.ChunkSize)})
		}
	}
	_, err := fmt.Fprintln(out, txtfmt.FormatEntity("", rows))
	return err
}

type containerInfo struct {
	dci              C.daos_cont_info_t
	PoolUUID         *uuid.UUID `json:"pool_uuid"`
	ContainerUUID    *uuid.UUID `json:"container_uuid"`
	ContainerLabel   string     `json:"container_label,omitempty"`
	LatestSnapshot   *uint64    `json:"latest_snapshot"`
	RedundancyFactor uint32     `json:"redundancy_factor"`
	NumHandles       *uint32    `json:"num_handles"`
	NumSnapshots     *uint32    `json:"num_snapshots"`
	OpenTime         *uint64    `json:"open_time"`
	CloseModifyTime  *uint64    `json:"close_modify_time"`
	Type             string     `json:"container_type"`
	ObjectClass      string     `json:"object_class,omitempty"`
	DirObjectClass   string     `json:"dir_object_class,omitempty"`
	FileObjectClass  string     `json:"file_object_class,omitempty"`
	CHints           string     `json:"hints,omitempty"`
	ChunkSize        uint64     `json:"chunk_size,omitempty"`
}

func (ci *containerInfo) MarshalJSON() ([]byte, error) {
	type toJSON containerInfo
	return json.Marshal(&struct {
		*toJSON
	}{
		toJSON: (*toJSON)(ci),
	})
}

// as an experiment, try creating a Go struct whose members are
// pointers into the C struct.
func newContainerInfo(poolUUID, contUUID *uuid.UUID) *containerInfo {
	ci := new(containerInfo)

	ci.PoolUUID = poolUUID
	ci.ContainerUUID = contUUID
	ci.LatestSnapshot = (*uint64)(&ci.dci.ci_lsnapshot)
	ci.NumHandles = (*uint32)(&ci.dci.ci_nhandles)
	ci.NumSnapshots = (*uint32)(&ci.dci.ci_nsnapshots)
	ci.OpenTime = (*uint64)(&ci.dci.ci_md_otime)
	ci.CloseModifyTime = (*uint64)(&ci.dci.ci_md_mtime)
	return ci
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

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RO, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	ci, err := cmd.queryContainer()
	if err != nil {
		return errors.Wrapf(err,
			"failed to query container %s",
			cmd.contUUID)
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(ci, nil)
	}

	var bld strings.Builder
	if err := printContainerInfo(&bld, ci, true); err != nil {
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
	cmd.Infof("Successfully created container %s", C.GoString(&ap.dm_args.dst_cont[0]))
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

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RW, ap)
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

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RO, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	attrs, err := listDaosAttributes(cmd.cContHandle, contAttr, cmd.Verbose)
	if err != nil {
		return errors.Wrapf(err,
			"failed to list attributes for container %s",
			cmd.ContainerID())
	}

	if cmd.JSONOutputEnabled() {
		if cmd.Verbose {
			return cmd.OutputJSON(attrs.asMap(), nil)
		}
		return cmd.OutputJSON(attrs.asList(), nil)
	}

	var bld strings.Builder
	title := fmt.Sprintf("Attributes for container %s:", cmd.ContainerID())
	printAttributes(&bld, title, attrs...)

	cmd.Info(bld.String())

	return nil
}

type containerDelAttrCmd struct {
	existingContainerCmd

	FlagAttr string `long:"attr" short:"a" description:"attribute name (deprecated; use positional argument)"`
	Args     struct {
		Attr string `positional-arg-name:"<attribute name>"`
	} `positional-args:"yes"`
}

func (cmd *containerDelAttrCmd) Execute(args []string) error {
	if cmd.FlagAttr != "" {
		cmd.Args.Attr = cmd.FlagAttr
	}
	if cmd.Args.Attr == "" {
		return errors.New("attribute name is required")
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

	if err := delDaosAttribute(cmd.cContHandle, contAttr, cmd.Args.Attr); err != nil {
		return errors.Wrapf(err,
			"failed to delete attribute %q on container %s",
			cmd.Args.Attr, cmd.ContainerID())
	}

	return nil
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
		cmd.Args.Attrs.ParsedProps.Add(cmd.FlagAttr)
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

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RO, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	var attrs attrList
	if len(cmd.Args.Attrs.ParsedProps) == 0 {
		attrs, err = listDaosAttributes(cmd.cContHandle, contAttr, true)
	} else {
		cmd.Debugf("getting attributes: %s", cmd.Args.Attrs.ParsedProps)
		attrs, err = getDaosAttributes(cmd.cContHandle, contAttr, cmd.Args.Attrs.ParsedProps.ToSlice())
	}
	if err != nil {
		return errors.Wrapf(err, "failed to get attributes from container %s", cmd.ContainerID())
	}

	if cmd.JSONOutputEnabled() {
		// Maintain compatibility with older behavior.
		if len(cmd.Args.Attrs.ParsedProps) == 1 && len(attrs) == 1 {
			return cmd.OutputJSON(attrs[0], nil)
		}
		return cmd.OutputJSON(attrs, nil)
	}

	var bld strings.Builder
	title := fmt.Sprintf("Attributes for container %s:", cmd.ContainerID())
	printAttributes(&bld, title, attrs...)

	cmd.Info(bld.String())

	return nil
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
	} else if len(args) > 0 {
		if err := cmd.Args.Attrs.UnmarshalFlag(args[len(args)-1]); err != nil {
			return err
		}
	}

	if len(cmd.Args.Attrs.ParsedProps) == 0 {
		return errors.New("attribute name and value are required")
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

	attrs := make(attrList, 0, len(cmd.Args.Attrs.ParsedProps))
	for key, val := range cmd.Args.Attrs.ParsedProps {
		attrs = append(attrs, &attribute{
			Name:  key,
			Value: []byte(val),
		})
	}

	if err := setDaosAttributes(cmd.cContHandle, contAttr, attrs); err != nil {
		return errors.Wrapf(err, "failed to set attributes on container %s", cmd.ContainerID())
	}

	return nil
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

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RO, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	props, freeProps, err := getContainerProperties(cmd.cContHandle, cmd.Args.Props.names...)
	defer freeProps()
	if err != nil {
		return errors.Wrapf(err,
			"failed to fetch properties for container %s",
			cmd.ContainerID())
	}

	if len(cmd.Args.Props.names) == len(propHdlrs) {
		aclProps, cleanupAcl, err := getContAcl(cmd.cContHandle)
		if err != nil && err != daos.NoPermission {
			return errors.Wrapf(err,
				"failed to query ACL for container %s",
				cmd.ContainerID())
		}
		if cleanupAcl != nil {
			defer cleanupAcl()
		}
		for _, prop := range aclProps {
			if prop.entry.dpe_type == C.DAOS_PROP_CO_ACL {
				props = append(props, prop)
				break
			}
		}
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(props, nil)
	}

	title := fmt.Sprintf("Properties for container %s", cmd.ContainerID())
	var bld strings.Builder
	printProperties(&bld, title, props...)

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
		defer cmd.Args.Props.Cleanup()
	}
	if len(cmd.PropsFlag.ParsedProps) > 0 {
		if len(cmd.Args.Props.ParsedProps) > 0 {
			return errors.New("cannot specify both --properties and positional arguments")
		}
		cmd.Args.Props = cmd.PropsFlag
	}
	if len(cmd.Args.Props.ParsedProps) == 0 {
		return errors.New("property name and value are required")
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

	rc := C.daos_cont_set_prop(ap.cont, cmd.Args.Props.props, nil)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err, "failed to set properties on container %s",
			cmd.ContainerID())
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

	cleanup, err := pf.resolveAndConnect(C.DAOS_PC_RO, nil)
	if err != nil {
		return
	}
	defer cleanup()

	contIDs, err := listContainers(pf.cPoolHandle)
	if err != nil {
		return
	}

	for _, id := range contIDs {
		if strings.HasPrefix(id.String(), match) {
			comps = append(comps, flags.Completion{
				Item: id.String(),
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

	var co_flags C.uint
	if cmd.All {
		co_flags = C.DAOS_COO_EVICT_ALL | C.DAOS_COO_EX
	} else {
		co_flags = C.DAOS_COO_EVICT | C.DAOS_COO_RO
	}

	cleanup, err := cmd.resolveAndConnect(co_flags, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	return nil
}
