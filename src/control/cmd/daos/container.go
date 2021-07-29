//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"strings"
	"unsafe"

	"github.com/dustin/go-humanize"
	"github.com/google/uuid"
	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/drpc"
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
	List        containerListCmd        `command:"list" alias:"ls" description:"list all containers in pool"`
	Destroy     containerDestroyCmd     `command:"destroy" description:"destroy a container"`
	ListObjects containerListObjectsCmd `command:"list-objects" alias:"list-obj" description:"list all objects in container"`
	Query       containerQueryCmd       `command:"query" description:"query a container"`
	Stat        containerStatCmd        `command:"stat" description:"get container statistics"`
	Clone       containerCloneCmd       `command:"clone" description:"clone a container"`
	Check       containerCheckCmd       `command:"check" description:"check objects' consistency in a container"`

	ListAttributes  containerListAttributesCmd  `command:"list-attr" alias:"list-attrs" alias:"lsattr" description:"list container user-defined attributes"`
	DeleteAttribute containerDeleteAttributeCmd `command:"del-attr" alias:"delattr" description:"delete container user-defined attribute"`
	GetAttribute    containerGetAttributeCmd    `command:"get-attr" alias:"getattr" description:"get container user-defined attribute"`
	SetAttribute    containerSetAttributeCmd    `command:"set-attr" alias:"setattr" description:"set container user-defined attribute"`

	GetProperty containerGetPropertyCmd `command:"get-prop" alias:"getprop" description:"get container user-defined attribute"`
	SetProperty containerSetPropertyCmd `command:"set-prop" alias:"setprop" description:"set container user-defined attribute"`

	GetACL       containerGetACLCmd       `command:"get-acl" description:"get a container's ACL"`
	OverwriteACL containerOverwriteACLCmd `command:"overwrite-acl" alias:"replace" description:"replace a container's ACL"`
	UpdateACL    containerUpdateACLCmd    `command:"update-acl" description:"update a container's ACL"`
	DeleteACL    containerDeleteACLCmd    `command:"delete-acl" description:"delete a container's ACL"`
	SetOwner     containerSetOwnerCmd     `command:"set-owner" alias:"chown" description:"change ownership for a container"`

	CreateSnapshot   containerSnapshotCreateCmd   `command:"create-snap" alias:"snap" description:"create container snapshot"`
	DestroySnapshot  containerSnapshotDestroyCmd  `command:"destroy-snap" description:"destroy container snapshot"`
	ListSnapshots    containerSnapshotListCmd     `command:"list-snap" alias:"list-snaps" description:"list container snapshots"`
	RollbackSnapshot containerSnapshotRollbackCmd `command:"rollback" description:"roll back container to specified snapshot"`
}

type containerBaseCmd struct {
	poolBaseCmd
	contUUID  uuid.UUID
	contLabel string

	cContHandle C.daos_handle_t
}

func (cmd *containerBaseCmd) contUUIDPtr() *C.uchar {
	if cmd.contUUID == uuid.Nil {
		cmd.log.Error("contUUIDPtr(): nil UUID")
		return nil
	}
	return (*C.uchar)(unsafe.Pointer(&cmd.contUUID[0]))
}

func (cmd *containerBaseCmd) openContainer() error {
	openFlags := C.uint(C.DAOS_COO_RW | C.DAOS_COO_FORCE)

	var rc C.int
	switch {
	case cmd.contLabel != "":
		var contInfo C.daos_cont_info_t
		cLabel := C.CString(cmd.contLabel)
		defer freeString(cLabel)

		cmd.log.Debugf("opening container: %s", cmd.contLabel)
		rc = C.daos_cont_open2(cmd.cPoolHandle, cLabel, openFlags,
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
		cmd.log.Debugf("opening container: %s", cmd.contUUID)
		cUUIDstr := C.CString(cmd.contUUID.String())
		defer freeString(cUUIDstr)
		rc = C.daos_cont_open2(cmd.cPoolHandle, cUUIDstr,
			openFlags, &cmd.cContHandle, nil, nil)
	default:
		return errors.New("no container UUID or label supplied")
	}

	return daosError(rc)
}

func (cmd *containerBaseCmd) closeContainer() error {
	cmd.log.Debugf("closing container: %s", cmd.contUUID)
	return daosError(C.daos_cont_close(cmd.cContHandle, nil))
}

func (cmd *containerBaseCmd) queryContainer() (*containerInfo, error) {
	ci := newContainerInfo(&cmd.poolUUID, &cmd.contUUID)
	var cType [10]C.char

	props, entries, err := allocProps(1)
	if err != nil {
		return nil, err
	}
	entries[0].dpe_type = C.DAOS_PROP_CO_LAYOUT_TYPE
	props.dpp_nr++
	defer func() { C.daos_prop_free(props) }()

	rc := C.daos_cont_query(cmd.cContHandle, &ci.dci, props, nil)
	if err := daosError(rc); err != nil {
		return nil, err
	}

	lType := C.get_dpe_val(&entries[0])
	C.daos_unparse_ctype(C.ushort(lType), &cType[0])
	ci.Type = C.GoString(&cType[0])

	if lType == C.DAOS_PROP_CO_LAYOUT_POSIX {
		var dfs *C.dfs_t
		var attr C.dfs_attr_t
		var oclass [10]C.char

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
		ci.ChunkSize = uint64(attr.da_chunk_size)

		if err := dfsError(C.dfs_umount(dfs)); err != nil {
			return nil, errors.Wrap(err, "failed to unmount container")
		}
	}

	return ci, nil
}

func (cmd *containerBaseCmd) connectPool(ap *C.struct_cmd_args_s) (func(), error) {
	if err := cmd.poolBaseCmd.connectPool(); err != nil {
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

	Type        string               `long:"type" short:"t" description:"container type" choice:"POSIX" choice:"HDF5"`
	Path        string               `long:"path" short:"d" description:"container namespace path"`
	ChunkSize   ChunkSizeFlag        `long:"chunk-size" short:"z" description:"container chunk size"`
	ObjectClass ObjClassFlag         `long:"oclass" short:"o" description:"default object class"`
	Properties  CreatePropertiesFlag `long:"properties" description:"container properties"`
	Label       string               `long:"label" short:"l" description:"container label"`
	Mode        ConsModeFlag         `long:"mode" short:"M" description:"DFS consistency mode"`
	ACLFile     string               `long:"acl-file" short:"A" description:"input file containing ACL"`
	User        string               `long:"user" short:"u" description:"user who will own the container (username@[domain])"`
	Group       string               `long:"group" short:"g" description:"group who will own the container (group@[domain])"`
	ContFlag    ContainerID          `long:"cont" short:"c" description:"container UUID (optional)"`
	Args        struct {
		Container ContainerID `positional-arg-name:"<container UUID (optional)>"`
	} `positional-args:"yes"`
}

func (cmd *containerCreateCmd) getUserUUID() uuid.UUID {
	for _, id := range []ContainerID{cmd.Args.Container, cmd.ContFlag} {
		if id.HasUUID() {
			return id.UUID
		}
	}
	return uuid.Nil
}

func (cmd *containerCreateCmd) Execute(_ []string) (err error) {
	if cu := cmd.getUserUUID(); cu != uuid.Nil {
		cmd.contUUID = cu
	} else {
		cmd.contUUID = uuid.New()
	}

	ap, deallocCmdArgs, err := allocCmdArgs(cmd.log)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	disconnectPool, err := cmd.connectPool(ap)
	if err != nil {
		return err
	}
	defer disconnectPool()

	if err := copyUUID(&ap.c_uuid, cmd.contUUID); err != nil {
		return err
	}
	ap.c_op = C.CONT_CREATE

	if cmd.User != "" {
		ap.user = C.CString(cmd.User)
		defer freeString(ap.user)
	}
	if cmd.Group != "" {
		ap.group = C.CString(cmd.Group)
		defer freeString(ap.group)
	}
	if cmd.ACLFile != "" {
		ap.aclfile = C.CString(cmd.ACLFile)
		defer freeString(ap.aclfile)
	}

	if cmd.Label != "" {
		for key := range cmd.Properties.ParsedProps {
			if key == "label" {
				return errors.New("can't use both --label and --properties label:")
			}
		}
		if err := cmd.Properties.AddPropVal("label", cmd.Label); err != nil {
			return err
		}
	}

	if cmd.Properties.props != nil {
		ap.props = cmd.Properties.props
	}

	switch cmd.Type {
	case "POSIX":
		ap._type = C.DAOS_PROP_CO_LAYOUT_POSIX

		if cmd.ChunkSize.Set {
			ap.chunk_size = cmd.ChunkSize.Size
		}
		if cmd.ObjectClass.Set {
			ap.oclass = cmd.ObjectClass.Class
		}
		if cmd.Mode.Set {
			ap.mode = cmd.Mode.Mode
		}
	case "HDF5":
		ap._type = C.DAOS_PROP_CO_LAYOUT_HDF5
	}

	var rc C.int
	if cmd.Path != "" {
		ap.path = C.CString(cmd.Path)
		defer freeString(ap.path)
		rc = C.cont_create_uns_hdlr(ap)
	} else {
		rc = C.cont_create_hdlr(ap)
	}
	if err := daosError(rc); err != nil {
		return errors.Wrap(err, "failed to create container")
	}
	cmd.log.Debugf("created container: %s", cmd.contUUID)

	if err := cmd.openContainer(); err != nil {
		return errors.Wrapf(err,
			"failed to open new container %s", cmd.contUUID)
	}
	defer cmd.closeContainer()

	ci, err := cmd.queryContainer()
	if err != nil {
		return errors.Wrapf(err,
			"failed to query new container %s",
			cmd.contUUID)
	}
	if label, set := cmd.Properties.ParsedProps["label"]; set {
		ci.ContainerLabel = label
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(ci, nil)
	}

	var bld strings.Builder
	if err := printContainerInfo(&bld, ci, false); err != nil {
		return err
	}
	cmd.log.Info(bld.String())

	return nil
}

type existingContainerCmd struct {
	containerBaseCmd

	Path     string      `long:"path" short:"d" description:"unified namespace path"`
	ContFlag ContainerID `long:"cont" short:"c" description:"container UUID (deprecated; use positional arg)"`
	Args     struct {
		Container ContainerID `positional-arg-name:"<container name or UUID>"`
	} `positional-args:"yes"`
}

func (cmd *existingContainerCmd) ContainerID() ContainerID {
	if !cmd.ContFlag.Empty() {
		return cmd.ContFlag
	}
	return cmd.Args.Container
}

func (cmd *existingContainerCmd) resolveContainer(ap *C.struct_cmd_args_s) (err error) {
	switch {
	case cmd.Path != "" && !(cmd.PoolID().Empty() && cmd.ContainerID().Empty()):
		return errors.New("can't specify --path with pool ID or container ID")
	case cmd.Path == "" && (cmd.PoolID().Empty() || cmd.ContainerID().Empty()):
		return errors.New("pool and container ID must be specified if --path not used")
	}

	if cmd.Path != "" {
		if ap == nil {
			return errors.New("ap cannot be nil with --path")
		}
		if err = resolveDunsPath(cmd.Path, ap); err != nil {
			return
		}

		if ap.pool_label != nil {
			cmd.poolBaseCmd.Args.Pool.Label = C.GoString(ap.pool_label)
			freeString(ap.pool_label)
		} else {
			cmd.poolBaseCmd.Args.Pool.UUID, err = uuidFromC(ap.p_uuid)
			if err != nil {
				return
			}
		}

		if ap.cont_label != nil {
			cmd.contLabel = C.GoString(ap.cont_label)
			freeString(ap.cont_label)
		} else {
			cmd.Args.Container.UUID, err = uuidFromC(ap.c_uuid)
			if err != nil {
				return
			}
		}
		cmd.contUUID = cmd.Args.Container.UUID
	} else {
		switch {
		case cmd.ContainerID().HasLabel():
			cmd.contLabel = cmd.ContainerID().Label
		case cmd.ContainerID().HasUUID():
			cmd.contUUID = cmd.ContainerID().UUID
		default:
			return errors.New("no container label or UUID supplied")
		}
	}

	cmd.log.Debugf("pool ID: %s, container ID: %s",
		cmd.PoolID(), cmd.ContainerID())

	return nil
}

func (cmd *existingContainerCmd) resolveAndConnect(ap *C.struct_cmd_args_s) (cleanFn func(), err error) {
	if err = cmd.resolveContainer(ap); err != nil {
		return
	}

	var cleanupPool func()
	cleanupPool, err = cmd.connectPool(ap)
	if err != nil {
		return
	}

	if err = cmd.openContainer(); err != nil {
		return
	}

	if ap != nil {
		if err = copyUUID(&ap.c_uuid, cmd.contUUID); err != nil {
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
	cleanup, err := cmd.resolveAndConnect(nil)
	if err != nil {
		return err
	}
	defer cleanup()

	contIDs, err := listContainers(cmd.cPoolHandle)
	if err != nil {
		return errors.Wrapf(err,
			"unable to list containers for pool %s", cmd.PoolID())
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(contIDs, nil)
	}

	var bld strings.Builder
	printContainers(&bld, contIDs)
	cmd.log.Info(bld.String())

	return nil
}

type containerDestroyCmd struct {
	existingContainerCmd

	Force bool `long:"force" short:"f" description:"force the container destroy"`
}

func (cmd *containerDestroyCmd) Execute(_ []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.log)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	if err := cmd.resolveContainer(ap); err != nil {
		return err
	}

	cleanup, err := cmd.connectPool(ap)
	if err != nil {
		return err
	}
	defer cleanup()

	cmd.log.Debugf("destroying container %s (force: %t)",
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
		rc = C.daos_cont_destroy2(cmd.cPoolHandle, cUUIDstr,
			goBool2int(cmd.Force), nil)
	case cmd.ContainerID().Label != "":
		cLabel := C.CString(cmd.ContainerID().Label)
		defer freeString(cLabel)
		rc = C.daos_cont_destroy2(cmd.cPoolHandle,
			cLabel, goBool2int(cmd.Force), nil)
	default:
		return errors.New("no UUID or label or path for container")
	}

	if err := daosError(rc); err != nil {
		return errors.Wrapf(err,
			"failed to destroy container %s",
			cmd.ContainerID())
	}

	cmd.log.Infof("Successfully destroyed container %s", cmd.ContainerID())

	return nil
}

type containerListObjectsCmd struct {
	existingContainerCmd

	Epoch uint64 `long:"epc" short:"e" description:"container epoch"`
}

func (cmd *containerListObjectsCmd) Execute(_ []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.log)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(ap)
	if err != nil {
		return err
	}
	defer cleanup()

	if cmd.Epoch > 0 {
		ap.epc = C.uint64_t(cmd.Epoch)
	}

	// TODO: Build a Go slice so that we can JSON-format the list.
	rc := C.cont_list_objs_hdlr(ap)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err,
			"failed to list objects in container %s", cmd.ContainerID())
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
	epochs := ci.SnapshotEpochs()
	epochStrs := make([]string, *ci.NumSnapshots)
	for i := uint32(0); i < *ci.NumSnapshots; i++ {
		epochStrs[i] = fmt.Sprintf("%d", epochs[i])
	}

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
			{"Number of snapshots": fmt.Sprintf("%d", *ci.NumSnapshots)},
			{"Latest Persistent Snapshot": fmt.Sprintf("%d", *ci.LatestSnapshot)},
			{"Highest Aggregated Epoch": fmt.Sprintf("%d", *ci.HighestAggregatedEpoch)},
			{"Container redundancy factor": fmt.Sprintf("%d", *ci.RedundancyFactor)},
			{"Snapshot Epochs": strings.Join(epochStrs, ",")},
		}...)

		if ci.ObjectClass != "" {
			rows = append(rows, txtfmt.TableRow{"Object Class": ci.ObjectClass})
		}
		if ci.ChunkSize > 0 {
			rows = append(rows, txtfmt.TableRow{"Chunk Size": humanize.IBytes(ci.ChunkSize)})
		}
	}
	_, err := fmt.Fprintln(out, txtfmt.FormatEntity("", rows))
	return err
}

type containerInfo struct {
	dci                    C.daos_cont_info_t
	PoolUUID               *uuid.UUID `json:"pool_uuid"`
	ContainerUUID          *uuid.UUID `json:"container_uuid"`
	ContainerLabel         string     `json:"container_label,omitempty"`
	LatestSnapshot         *uint64    `json:"latest_snapshot"`
	RedundancyFactor       *uint32    `json:"redundancy_factor"`
	NumSnapshots           *uint32    `json:"num_snapshots"`
	HighestAggregatedEpoch *uint64    `json:"highest_aggregated_epoch"`
	Type                   string     `json:"container_type"`
	ObjectClass            string     `json:"object_class,omitempty"`
	ChunkSize              uint64     `json:"chunk_size,omitempty"`
}

func (ci *containerInfo) SnapshotEpochs() []uint64 {
	if ci.dci.ci_snapshots == nil {
		return nil
	}

	// return a Go slice backed by the C array of snapshot epochs
	return (*[1 << 30]uint64)(unsafe.Pointer(ci.dci.ci_snapshots))[:*ci.NumSnapshots:*ci.NumSnapshots]
}

func (ci *containerInfo) MarshalJSON() ([]byte, error) {
	type toJSON containerInfo
	return json.Marshal(&struct {
		SnapshotEpochs []uint64 `json:"snapshot_epochs"`
		*toJSON
	}{
		SnapshotEpochs: ci.SnapshotEpochs(),
		toJSON:         (*toJSON)(ci),
	})
}

// as an experiment, try creating a Go struct whose members are
// pointers into the C struct.
func newContainerInfo(poolUUID, contUUID *uuid.UUID) *containerInfo {
	ci := new(containerInfo)

	ci.PoolUUID = poolUUID
	ci.ContainerUUID = contUUID
	ci.LatestSnapshot = (*uint64)(&ci.dci.ci_lsnapshot)
	ci.RedundancyFactor = (*uint32)(&ci.dci.ci_redun_fac)
	ci.NumSnapshots = (*uint32)(&ci.dci.ci_nsnapshots)
	ci.HighestAggregatedEpoch = (*uint64)(&ci.dci.ci_hae)

	return ci
}

type containerQueryCmd struct {
	existingContainerCmd
}

func (cmd *containerQueryCmd) Execute(_ []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.log)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(ap)
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

	if cmd.contLabel != "" {
		ci.ContainerLabel = cmd.contLabel
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(ci, nil)
	}

	var bld strings.Builder
	if err := printContainerInfo(&bld, ci, true); err != nil {
		return err
	}
	cmd.log.Info(bld.String())

	return nil
}

type containerCloneCmd struct {
	daosCmd

	Source      string `long:"src" short:"S" description:"source container" required:"1"`
	Destination string `long:"dst" short:"D" description:"destination container" required:"1"`
}

func (cmd *containerCloneCmd) Execute(_ []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.log)
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
		return errors.Wrapf(err,
			"failed to clone container %s",
			cmd.Source)
	}

	return nil
}

type containerCheckCmd struct {
	existingContainerCmd

	Epoch uint64 `long:"epc" short:"e" description:"container epoch"`
}

func (cmd *containerCheckCmd) Execute(_ []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.log)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(ap)
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

type containerListAttributesCmd struct {
	existingContainerCmd

	Verbose bool `long:"verbose" short:"V" description:"Include values"`
}

func (cmd *containerListAttributesCmd) Execute(args []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.log)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(ap)
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

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(attrs, nil)
	}

	var bld strings.Builder
	title := fmt.Sprintf("Attributes for container %s:", cmd.ContainerID())
	printAttributes(&bld, title, attrs...)

	cmd.log.Info(bld.String())

	return nil
}

type containerDeleteAttributeCmd struct {
	existingContainerCmd

	Args struct {
		Name string `positional-arg-name:"<attribute name>"`
	} `positional-args:"yes" required:"yes"`
}

func (cmd *containerDeleteAttributeCmd) Execute(args []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.log)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(ap)
	if err != nil {
		return err
	}
	defer cleanup()

	if err := delDaosAttribute(cmd.cContHandle, contAttr, cmd.Args.Name); err != nil {
		return errors.Wrapf(err,
			"failed to delete attribute %q on container %s",
			cmd.Args.Name, cmd.ContainerID())
	}

	return nil
}

type containerGetAttributeCmd struct {
	existingContainerCmd

	Args struct {
		Name string `positional-arg-name:"<attribute name>"`
	} `positional-args:"yes" required:"yes"`
}

func (cmd *containerGetAttributeCmd) Execute(args []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.log)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(ap)
	if err != nil {
		return err
	}
	defer cleanup()

	attr, err := cmd.getAttr(cmd.Args.Name)
	if err != nil {
		return errors.Wrapf(err,
			"failed to get attribute %q from container %s",
			cmd.Args.Name, cmd.ContainerID())
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(attr, nil)
	}

	var bld strings.Builder
	title := fmt.Sprintf("Attributes for container %s:", cmd.ContainerID())
	printAttributes(&bld, title, attr)

	cmd.log.Info(bld.String())

	return nil
}

type containerSetAttributeCmd struct {
	existingContainerCmd

	Args struct {
		Attr  string `positional-arg-name:"<attribute name>"`
		Value string `positional-arg-name:"<attribute value>"`
	} `positional-args:"yes"`
	FlagAttr  string `long:"attr" short:"a" description:"attribute name"`
	FlagValue string `long:"value" short:"v" description:"attribute value"`
}

func (cmd *containerSetAttributeCmd) Execute(args []string) error {
	if cmd.FlagAttr != "" {
		cmd.Args.Attr = cmd.FlagAttr
	}
	if cmd.FlagValue != "" {
		cmd.Args.Value = cmd.FlagValue
	}

	ap, deallocCmdArgs, err := allocCmdArgs(cmd.log)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(ap)
	if err != nil {
		return err
	}
	defer cleanup()

	if err := setDaosAttribute(cmd.cContHandle, contAttr, &attribute{
		Name:  cmd.Args.Attr,
		Value: cmd.Args.Value,
	}); err != nil {
		return errors.Wrapf(err,
			"failed to set attribute %q on container %s",
			cmd.Args.Attr, cmd.ContainerID())
	}

	return nil
}

type containerGetPropertyCmd struct {
	existingContainerCmd

	Properties GetPropertiesFlag `long:"properties" description:"container properties to get" default:"all"`
}

func (cmd *containerGetPropertyCmd) Execute(args []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.log)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(ap)
	if err != nil {
		return err
	}
	defer cleanup()

	props, freeProps, err := getContainerProperties(cmd.cContHandle, cmd.Properties.names...)
	defer freeProps()
	if err != nil {
		return errors.Wrapf(err,
			"failed to fetch properties for container %s",
			cmd.ContainerID())
	}

	if len(cmd.Properties.names) == len(propHdlrs) {
		aclProps, cleanupAcl, err := getContAcl(cmd.cContHandle)
		if err != nil && err != drpc.DaosNoPermission {
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

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(props, nil)
	}

	title := fmt.Sprintf("Properties for container %s", cmd.ContainerID())
	var bld strings.Builder
	printProperties(&bld, title, props...)

	cmd.log.Info(bld.String())

	return nil
}

type containerSetPropertyCmd struct {
	existingContainerCmd

	Properties SetPropertiesFlag `long:"properties" required:"1" description:"container properties to set"`
}

func (cmd *containerSetPropertyCmd) Execute(args []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.log)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(ap)
	if err != nil {
		return err
	}
	defer cleanup()

	ap.props = cmd.Properties.props

	rc := C.cont_set_prop_hdlr(ap)
	if err := daosError(rc); err != nil {
		return errors.Errorf("failed to set properties on container %s",
			cmd.ContainerID())
	}

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
	ui.LabelOrUUIDFlag
}

// Implement the completion handler to provide a list of container IDs
// as completion items.
func (f *ContainerID) Complete(match string) (comps []flags.Completion) {
	pf := parsePoolFlag()
	pf.log = &logging.LeveledLogger{}

	fini, err := pf.initDAOS()
	if err != nil {
		return
	}
	defer fini()

	cleanup, err := pf.resolveAndConnect(nil)
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
