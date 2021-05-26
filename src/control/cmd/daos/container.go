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
	"github.com/daos-stack/daos/src/control/logging"
)

/*
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
	Check       containerCheckCmd       `command:"check" description:"check objects' consistency in a container"`

	ListAttributes  containerListAttributesCmd  `command:"list-attributes" alias:"list-attrs" description:"list container user-defined attributes"`
	DeleteAttribute containerDeleteAttributeCmd `command:"delete-attribute" alias:"del-attr" description:"delete container user-defined attribute"`
	GetAttribute    containerGetAttributeCmd    `command:"get-attribute" alias:"get-attr" description:"get container user-defined attribute"`
	SetAttribute    containerSetAttributeCmd    `command:"set-attribute" alias:"set-attr" description:"set container user-defined attribute"`

	GetProperty containerGetPropertyCmd `command:"get-property" alias:"get-prop" description:"get container user-defined attribute"`
	SetProperty containerSetPropertyCmd `command:"set-property" alias:"set-prop" description:"set container user-defined attribute"`

	GetACL       containerGetACLCmd       `command:"get-acl" description:"get a container's ACL"`
	OverwriteACL containerOverwriteACLCmd `command:"overwrite-acl" alias:"replace" description:"replace a container's ACL"`
	UpdateACL    containerUpdateACLCmd    `command:"update-acl" description:"update a container's ACL"`
	DeleteACL    containerDeleteACLCmd    `command:"delete-acl" description:"delete a container's ACL"`
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

func (cmd *containerBaseCmd) resolveContainer(id ContainerID) error {
	// TODO: Resolve label.
	if id.HasLabel() {
		return errors.New("no support for container labels yet")
	}

	if !id.HasUUID() {
		return errors.New("no container UUID provided")
	}
	cmd.contUUID = id.UUID

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

	UUID        string         `long:"cont" short:"c" description:"container UUID (optional)"`
	Type        string         `long:"type" short:"t" description:"container type" choice:"POSIX" choice:"HDF5" default:"POSIX"`
	Path        string         `long:"path" short:"d" description:"container namespace path"`
	ChunkSize   string         `long:"chunk-size" short:"z" description:"container chunk size"`
	ObjectClass string         `long:"oclass" short:"o" description:"default object class"`
	Properties  PropertiesFlag `long:"properties" description:"container properties"`
	ACLFile     string         `long:"acl-file" short:"A" description:"input file containing ACL"`
	User        string         `long:"user" short:"u" description:"user who will own the container (username@[domain])"`
	Group       string         `long:"group" short:"g" description:"group who will own the container (group@[domain])"`
}

func (cmd *containerCreateCmd) Execute(_ []string) (err error) {
	if err = cmd.resolvePool(cmd.PoolID()); err != nil {
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
			defer freeString(cObjClass)
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

	var rc C.int
	if cmd.Path != "" {
		ap.path = C.CString(cmd.Path)
		defer freeString(ap.path)
		rc = C.cont_create_uns_hdlr(ap)
	} else {
		rc = C.cont_create_hdlr(ap)
	}
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

func (cmd *existingContainerCmd) resolveAndConnect(ap *C.struct_cmd_args_s) (cleanFn func(), err error) {
	switch {
	case cmd.Path != "" && !(cmd.PoolID().Empty() && cmd.ContainerID().Empty()):
		return nil, errors.New("can't specify --path with pool ID or container ID")
	case cmd.Path == "" && (cmd.PoolID().Empty() || cmd.ContainerID().Empty()):
		return nil, errors.New("pool and container ID must be specified if --path not used")
	}

	// FIXME: Refactor this stuff a bit so that we have a clean line
	// between the UNS/non-UNS workflows. We shouldn't be copying
	// the UUIDs back into ap if we resolved them via UNS, for example.
	if cmd.Path != "" {
		if err = resolveDunsPath(cmd.Path, ap); err != nil {
			return
		}
		cmd.poolUUID, err = uuidFromC(ap.p_uuid)
		if err != nil {
			return
		}
		cmd.contUUID, err = uuidFromC(ap.c_uuid)
		if err != nil {
			return
		}
	}

	var cleanupPool func()
	cleanupPool, err = cmd.poolBaseCmd.resolveAndConnect(ap)
	if err != nil {
		return
	}

	if cmd.contUUID == uuid.Nil {
		if err = cmd.resolveContainer(cmd.ContainerID()); err != nil {
			return
		}
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

	Force bool `long:"force" short:"f" description:"force the container destroy"`
}

func (cmd *containerDestroyCmd) Execute(_ []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.log)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(ap)
	if err != nil {
		return err
	}
	_ = cmd.closeContainer()
	defer cleanup()

	rc := C.daos_cont_destroy(cmd.cPoolHandle,
		cmd.contUUIDPtr(), goBool2int(cmd.Force), nil)

	if err := daosError(rc); err != nil {
		return errors.Wrapf(err,
			"failed to destroy container %s",
			cmd.ContainerID())
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
		Name  string `positional-arg-name:"<attribute name>"`
		Value string `positional-arg-name:"<attribute value>"`
	} `positional-args:"yes" required:"yes"`
}

func (cmd *containerSetAttributeCmd) Execute(args []string) error {
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
		Name:  cmd.Args.Name,
		Value: cmd.Args.Value,
	}); err != nil {
		return errors.Wrapf(err,
			"failed to set attribute %q on container %s",
			cmd.Args.Name, cmd.ContainerID())
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

	props, err := getContainerProperties(cmd.cContHandle, nil, cmd.Properties.names...)
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
	labelOrUUID
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

	contIDs, err := poolListContainers(pf.cPoolHandle)
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
