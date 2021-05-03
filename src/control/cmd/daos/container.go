//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"encoding/json"
	"fmt"
	"io"
	"strings"
	"unsafe"

	"github.com/daos-stack/daos/src/control/lib/txtfmt"
	"github.com/dustin/go-humanize"
	"github.com/google/uuid"
	"github.com/pkg/errors"
)

/*
#cgo CFLAGS: -I${SRCDIR}/../../../utils
#cgo LDFLAGS: -ldaos_cmd_hdlrs -ldfs -lduns

#include <stdlib.h>

#include <daos.h>

#include "daos_hdlr.h"
*/
import "C"

type containerCmd struct {
	Create      containerCreateCmd      `command:"create" description:"create a container"`
	Destroy     containerDestroyCmd     `command:"destroy" description:"destroy a container"`
	ListObjects containerListObjectsCmd `command:"list-objects" alias:"list-obj" description:"list all objects in container"`
	Query       containerQueryCmd       `command:"query" description:"query a container"`
	Stat        containerStatCmd        `command:"stat" description:"get container statistics"`
	Clone       containerCloneCmd       `command:"clone" description:"clone a container"`

	ListAttributes  containerListAttributesCmd  `command:"list-attributes" alias:"list-attrs" description:"list container user-defined attributes"`
	DeleteAttribute containerDeleteAttributeCmd `command:"delete-attribute" alias:"del-attr" description:"delete container user-defined attribute"`
	GetAttribute    containerGetAttributeCmd    `command:"get-attribute" alias:"get-attr" description:"get container user-defined attribute"`
	SetAttribute    containerSetAttributeCmd    `command:"set-attribute" alias:"set-attr" description:"set container user-defined attribute"`

	ListProperties containerListPropertiesCmd `command:"list-properties" alias:"list-props" description:"list container user-defined attributes"`
	DeleteProperty containerDeletePropertyCmd `command:"delete-property" alias:"del-prop" description:"delete container user-defined attribute"`
	GetProperty    containerGetPropertyCmd    `command:"get-property" alias:"get-prop" description:"get container user-defined attribute"`
	SetProperty    containerSetPropertyCmd    `command:"set-property" alias:"set-prop" description:"set container user-defined attribute"`

	GetACL       containerGetACLCmd       `command:"get" description:"get a container's ACL"`
	OverwriteACL containerOverwriteACLCmd `command:"overwrite" alias:"replace" description:"replace a container's ACL"`
	UpdateACL    containerUpdateACLCmd    `command:"update" description:"update a container's ACL"`
	DeleteACL    containerDeleteACLCmd    `command:"delete" description:"delete a container's ACL"`
	SetOwner     containerSetOwnerCmd     `command:"set-owner" description:"change ownership for a container"`

	CreateSnapshot   containerSnapshotCreateCmd   `command:"create-snapshot" alias:"create-snap" description:"create container snapshot"`
	DestroySnapshot  containerSnapshotDestroyCmd  `command:"destroy-snapshot" alias:"destroy-snap" description:"destroy container snapshot"`
	ListSnapshots    containerSnapshotListCmd     `command:"list-snapshots" alias:"list-snaps" description:"list container snapshots"`
	RollbackSnapshot containerSnapshotRollbackCmd `command:"rollback" alias:"rb" description:"roll back container to specified snapshot"`
}

type containerBaseCmd struct {
	poolBaseCmd
	contUUID uuid.UUID

	cContHandle C.daos_handle_t
}

func (cmd *containerBaseCmd) contUUIDPtr() *C.uchar {
	return (*C.uchar)(unsafe.Pointer(&cmd.contUUID[0]))
}

func (cmd *containerBaseCmd) resolveContainer(id string) (err error) {
	// TODO: Resolve label.

	cmd.contUUID, err = uuid.Parse(id)
	if err != nil {
		return errors.Wrap(err, "")
	}

	return nil
}

func (cmd *containerBaseCmd) openContainer() error {
	var ci C.daos_cont_info_t
	rc := C.daos_cont_open(cmd.cPoolHandle, cmd.contUUIDPtr(),
		C.DAOS_COO_RW|C.DAOS_COO_FORCE, &cmd.cContHandle, &ci, nil)
	return daosError(rc)
}

func (cmd *containerBaseCmd) closeContainer() error {
	cmd.log.Debugf("closing container %s", cmd.contUUID)
	return daosError(C.daos_cont_close(cmd.cContHandle, nil))
}

func (cmd *containerBaseCmd) queryContainer() (*containerInfo, error) {
	ci := newContainerInfo(&cmd.poolUUID, &cmd.contUUID)

	rc := C.daos_cont_query(cmd.cContHandle, &ci.dci, nil, nil)
	if err := daosError(rc); err != nil {
		return nil, err
	}

	return ci, nil
}

type containerCreateCmd struct {
	containerBaseCmd

	UUID        string         `long:"uuid" description:"container UUID (optional)"`
	Type        string         `long:"type" description:"container type" choice:"POSIX" choice:"HDF5" default:"POSIX"`
	ChunkSize   string         `long:"chunk-size" description:"container chunk size"`
	ObjectClass string         `long:"object-class" description:"default object class"`
	Properties  PropertiesFlag `long:"properties" description:"container properties"`
	ACLFile     string         `long:"acl-file" description:"input file containing ACL"`
	User        string         `long:"user" description:"user who will own the container (username@[domain])"`
	Group       string         `long:"group" description:"group who will own the container (group@[domain])"`
}

func (cmd *containerCreateCmd) Execute(_ []string) (err error) {
	if err = cmd.resolvePool(cmd.Args.Pool); err != nil {
		return
	}

	if cmd.UUID != "" {
		cmd.contUUID, err = uuid.Parse(cmd.UUID)
		if err != nil {
			return
		}
	} else {
		cmd.contUUID = uuid.New()
	}

	ap, deallocCmdArgs, err := allocCmdArgs(cmd.log)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	if err := cmd.connectPool(); err != nil {
		return err
	}
	defer cmd.disconnectPool()

	ap.pool = cmd.cPoolHandle
	if err := copyUUID(&ap.p_uuid, cmd.poolUUID); err != nil {
		return err
	}
	if err := copyUUID(&ap.c_uuid, cmd.contUUID); err != nil {
		return err
	}
	ap.c_op = C.CONT_CREATE

	if cmd.User != "" {
		ap.user = C.CString(cmd.User)
		defer C.free(unsafe.Pointer(ap.user))
	}
	if cmd.Group != "" {
		ap.group = C.CString(cmd.Group)
		defer C.free(unsafe.Pointer(ap.group))
	}
	if cmd.ACLFile != "" {
		ap.aclfile = C.CString(cmd.ACLFile)
		defer C.free(unsafe.Pointer(ap.aclfile))
	}
	if cmd.Properties.props != nil {
		ap.props = cmd.Properties.props
	}

	switch cmd.Type {
	case "POSIX":
		ap._type = C.DAOS_PROP_CO_LAYOUT_POSIX

		if cmd.ChunkSize != "" {
			chunkSize, err := humanize.ParseBytes(cmd.ChunkSize)
			if err != nil {
				return err
			}
			ap.chunk_size = C.ulong(chunkSize)
		}

		if cmd.ObjectClass != "" {
			cObjClass := C.CString(cmd.ObjectClass)
			defer C.free(unsafe.Pointer(cObjClass))
			ap.oclass = (C.ushort)(C.daos_oclass_name2id(cObjClass))
			if ap.oclass == C.OC_UNKNOWN {
				return errors.Errorf("unknown object class %q",
					cmd.ObjectClass)
			}
		}
	case "HDF5":
		ap._type = C.DAOS_PROP_CO_LAYOUT_HDF5
	default:
		return errors.Errorf("unknown container type %q", cmd.Type)
	}

	rc := C.cont_create_hdlr(ap)
	if err := daosError(rc); err != nil {
		return err
	}

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

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(ci, nil)
	}

	var bld strings.Builder
	if err := printContainerInfo(&bld, ci); err != nil {
		return err
	}
	cmd.log.Info(bld.String())

	return nil
}

type existingContainerCmd struct {
	containerBaseCmd

	Args struct {
		Container string `positional-arg-name:"<container name or UUID>"`
	} `positional-args:"yes" required:"yes"`
}

func (cmd *existingContainerCmd) resolveAndConnect(ap *C.struct_cmd_args_s) (cleanFn func(), err error) {
	var cleanupPool func()
	cleanupPool, err = cmd.poolBaseCmd.resolveAndConnect(ap)
	if err != nil {
		return
	}

	if err = cmd.resolveContainer(cmd.Args.Container); err != nil {
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

type containerDestroyCmd struct {
	existingContainerCmd

	Force bool `long:"force" description:"force the container destroy"`
}

func (cmd *containerDestroyCmd) Execute(_ []string) error {
	cleanup, err := cmd.resolveAndConnect(nil)
	if err != nil {
		return nil
	}
	defer cleanup()

	rc := C.daos_cont_destroy(cmd.cPoolHandle,
		cmd.contUUIDPtr(), goBool2int(cmd.Force), nil)

	if err := daosError(rc); err != nil {
		return errors.Wrapf(err,
			"failed to destroy container %s", cmd.contUUID)
	}

	return nil
}

type containerListObjectsCmd struct {
	existingContainerCmd
}

func (cmd *containerListObjectsCmd) Execute(_ []string) error {
	return nil
}

type containerStatCmd struct {
	existingContainerCmd
}

func (cmd *containerStatCmd) Execute(_ []string) error {
	return nil
}

func printContainerInfo(out io.Writer, ci *containerInfo) error {
	epochs := ci.SnapshotEpochs()
	epochStrs := make([]string, *ci.NumSnapshots)
	for i := uint32(0); i < *ci.NumSnapshots; i++ {
		epochStrs[i] = fmt.Sprintf("%d", epochs[i])
	}

	_, err := fmt.Fprintln(out, txtfmt.FormatEntity("", []txtfmt.TableRow{
		{"Pool UUID": ci.PoolUUID.String()},
		{"Container UUID": ci.ContainerUUID.String()},
		{"Number of snapshots": fmt.Sprintf("%d", *ci.NumSnapshots)},
		{"Latest Persistent Snapshot": fmt.Sprintf("%d", *ci.LatestSnapshot)},
		{"Highest Aggregated Epoch": fmt.Sprintf("%d", *ci.HighestAggregatedEpoch)},
		{"Container redundancy factor": fmt.Sprintf("%d", *ci.RedundancyFactor)},
		{"Snapshot Epochs": strings.Join(epochStrs, ",")},
	}))

	return err
}

type containerInfo struct {
	dci                    C.daos_cont_info_t
	PoolUUID               *uuid.UUID `json:"pool_uuid"`
	ContainerUUID          *uuid.UUID `json:"container_uuid"`
	LatestSnapshot         *uint64    `json:"latest_snapshot"`
	RedundancyFactor       *uint32    `json:"redundancy_factor"`
	NumSnapshots           *uint32    `json:"num_snapshots"`
	HighestAggregatedEpoch *uint64    `json:"highest_aggregated_epoch"`
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
	cleanup, err := cmd.resolveAndConnect(nil)
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

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(ci, nil)
	}

	var bld strings.Builder
	if err := printContainerInfo(&bld, ci); err != nil {
		return err
	}
	cmd.log.Info(bld.String())

	return nil
}

type containerCloneCmd struct {
	existingContainerCmd
}

func (cmd *containerCloneCmd) Execute(_ []string) error {
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

	ap.c_op = C.CONT_CLONE
	rc := C.cont_clone_hdlr(ap)

	if err := daosError(rc); err != nil {
		return errors.Wrapf(err,
			"failed to clone container %s", cmd.contUUID)
	}

	return nil
}

type containerListAttributesCmd struct {
	existingContainerCmd

	Verbose bool `long:"verbose" short:"v" description:"Include values"`
}

func (cmd *containerListAttributesCmd) Execute(args []string) error {
	cleanup, err := cmd.resolveAndConnect(nil)
	if err != nil {
		return err
	}
	defer cleanup()

	attrs, err := listDaosAttributes(cmd.cContHandle, contAttr, cmd.Verbose)
	if err != nil {
		return errors.Wrapf(err,
			"failed to list attributes for container %s", cmd.contUUID)
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(attrs, nil)
	}

	var bld strings.Builder
	title := fmt.Sprintf("Attributes for container %s:", cmd.contUUID)
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
	cleanup, err := cmd.resolveAndConnect(nil)
	if err != nil {
		return err
	}
	defer cleanup()

	if err := delDaosAttribute(cmd.cContHandle, contAttr, cmd.Args.Name); err != nil {
		return errors.Wrapf(err,
			"failed to delete attribute %q on container %s",
			cmd.Args.Name, cmd.contUUID)
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
	cleanup, err := cmd.resolveAndConnect(nil)
	if err != nil {
		return err
	}
	defer cleanup()

	attr, err := cmd.getAttr(cmd.Args.Name)
	if err != nil {
		return errors.Wrapf(err,
			"failed to get attribute %q from container %s",
			cmd.Args.Name, cmd.contUUID)
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(attr, nil)
	}

	var bld strings.Builder
	title := fmt.Sprintf("Attributes for container %s:", cmd.contUUID)
	printAttributes(&bld, title, attr)

	cmd.log.Info(bld.String())

	return nil
}

type containerSetAttributeCmd struct {
	existingContainerCmd

	Args struct {
		Name  string `positional-arg-name:"<attribute name>"`
		Value string `positional-arg-name:"<attribute value>"`
	} `positional-args:"yes" required:"yes"`
}

func (cmd *containerSetAttributeCmd) Execute(args []string) error {
	cleanup, err := cmd.resolveAndConnect(nil)
	if err != nil {
		return err
	}
	defer cleanup()

	if err := setDaosAttribute(cmd.cContHandle, contAttr, &attribute{
		Name:  cmd.Args.Name,
		Value: cmd.Args.Value,
	}); err != nil {
		return errors.Wrapf(err,
			"failed to set attribute %q on container %s",
			cmd.Args.Name, cmd.contUUID)
	}

	return nil
}

type containerListPropertiesCmd struct {
	existingContainerCmd
}

func (cmd *containerListPropertiesCmd) Execute(args []string) error {
	return nil
}

type containerDeletePropertyCmd struct {
	existingContainerCmd
}

func (cmd *containerDeletePropertyCmd) Execute(args []string) error {
	return nil
}

type containerGetPropertyCmd struct {
	existingContainerCmd
}

func (cmd *containerGetPropertyCmd) Execute(args []string) error {
	return nil
}

type containerSetPropertyCmd struct {
	existingContainerCmd

	Properties SetPropertiesFlag `long:"properties" required:"1" description:"container properties"`
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
			cmd.contUUID)
	}

	return nil
}
