//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"unsafe"

	"github.com/dustin/go-humanize"
	"github.com/google/uuid"
	"github.com/pkg/errors"
)

/*
#cgo CFLAGS: -I${SRCDIR}/../../../utils
#cgo LDFLAGS: -ldaos_cmd_hdlrs -ldfs -lduns

#include <stdlib.h>
#include "daos.h"
#include "daos_hdlr.h"
*/
import "C"

type containerCmd struct {
	Create      containerCreateCmd      `command:"create" description:"create a container"`
	Destroy     containerDestroyCmd     `command:"destroy" description:"destroy a container"`
	ListObjects containerListObjectsCmd `command:"list-objects" alias:"list-obj" description:"list all objects in container"`
	Query       containerQueryCmd       `command:"query" description:"query a container"`
	Stat        containerStatCmd        `command:"stat" description:"get container statistics"`
	Attribute   containerAttributeCmd   `command:"attribute" alias:"attr" description:"container attribute operations"`
	Snapshot    containerSnapshotCmd    `command:"snapshot"  description:"container snapshot operations"`
}

type containerBaseCmd struct {
	poolBaseCmd
	contUUID uuid.UUID

	cContHandle C.daos_handle_t
}

func (cmd *containerBaseCmd) contUUIDPtr() *C.uchar {
	return (*C.uchar)(unsafe.Pointer(&cmd.contUUID[0]))
}

type containerParamsCmd struct {
}

type containerCreateCmd struct {
	containerBaseCmd

	UUID        string `long:"uuid" description:"container UUID (optional)"`
	Name        string `long:"name" description:"container name (optional)"`
	Type        string `long:"type" description:"container type" choice:"POSIX" choice:"HDF5" default:"POSIX"`
	ChunkSize   string `long:"chunk-size" short:"z" description:"container chunk size"`
	ObjectClass string `long:"object-class" short:"o" description:"default object class"`
}

func (cmd *containerCreateCmd) Execute(args []string) (err error) {
	if err = cmd.resolvePool(); err != nil {
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

	if err := cmd.connectPool(); err != nil {
		return err
	}
	defer cmd.disconnectPool()

	ap, deallocCmdArgs, err := allocCmdArgs(cmd.log)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	ap.pool = cmd.cPoolHandle
	if err := copyUUID(&ap.p_uuid, cmd.poolUUID); err != nil {
		return err
	}
	if err := copyUUID(&ap.c_uuid, cmd.contUUID); err != nil {
		return err
	}
	ap.c_op = C.CONT_CREATE

	if cmd.Name != "" {
		// TODO: Set label prop
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
				return errors.Errorf("unknown object class %q", cmd.ObjectClass)
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

	// TODO: Query the container and return that output
	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(cmd.contUUID, nil)
	}

	return nil
}

type existingContainerCmd struct {
	containerBaseCmd

	Args struct {
		Container string `positional-arg-name:"<container name or UUID>"`
	} `positional-args:"yes" required:"yes"`
}

func (cmd *existingContainerCmd) resolveAndConnect() (cleanFn func(), err error) {
	if err = cmd.resolvePool(); err != nil {
		return
	}

	// TODO: Resolve name.
	cmd.contUUID, err = uuid.Parse(cmd.Args.Container)
	if err != nil {
		return
	}

	if err = cmd.connectPool(); err != nil {
		return
	}

	return func() {
		cmd.disconnectPool()
	}, nil
}

type containerDestroyCmd struct {
	existingContainerCmd

	Force bool `long:"force" description:"force the container destroy"`
}

func (cmd *containerDestroyCmd) Execute(args []string) error {
	cleanup, err := cmd.resolveAndConnect()
	if err != nil {
		return nil
	}
	defer cleanup()

	rc := C.daos_cont_destroy(cmd.cPoolHandle, cmd.contUUIDPtr(), goBool2int(cmd.Force), nil)

	return daosError(rc)
}

type containerListObjectsCmd struct {
	existingContainerCmd
}

func (cmd *containerListObjectsCmd) Execute(args []string) error {
	return nil
}

type containerStatCmd struct {
	existingContainerCmd
}

func (cmd *containerStatCmd) Execute(args []string) error {
	return nil
}

type containerQueryCmd struct {
	existingContainerCmd
}

func (cmd *containerQueryCmd) Execute(args []string) error {
	return nil
}

type containerAttributeCmd struct {
	List   containerListAttributesCmd  `command:"list" description:"list container user-defined attributes"`
	Delete containerDeleteAttributeCmd `command:"delete" description:"delete container user-defined attribute"`
	Get    containerGetAttributeCmd    `command:"get" description:"get container user-defined attribute"`
	Set    containerSetAttributeCmd    `command:"set" description:"set container user-defined attribute"`
}

type containerListAttributesCmd struct {
	existingContainerCmd
}

func (cmd *containerListAttributesCmd) Execute(args []string) error {
	return nil
}

type containerDeleteAttributeCmd struct {
	existingContainerCmd
}

func (cmd *containerDeleteAttributeCmd) Execute(args []string) error {
	return nil
}

type containerGetAttributeCmd struct {
	existingContainerCmd
}

func (cmd *containerGetAttributeCmd) Execute(args []string) error {
	return nil
}

type containerSetAttributeCmd struct {
	existingContainerCmd
}

func (cmd *containerSetAttributeCmd) Execute(args []string) error {
	return nil
}

type containerSnapshotCmd struct {
	Create   containerSnapshotCreateCmd   `command:"create" description:"create container snapshot"`
	Destroy  containerSnapshotDestroyCmd  `command:"destroy" description:"destroy container snapshot"`
	List     containerSnapshotListCmd     `command:"list" description:"list container snapshots"`
	Rollback containerSnapshotRollbackCmd `command:"rollback" alias:"rb" description:"roll back container to specified snapshot"`
}

type containerSnapshotCreateCmd struct {
	existingContainerCmd
}

func (cmd *containerSnapshotCreateCmd) Execute(args []string) error {
	return nil
}

type containerSnapshotDestroyCmd struct {
	existingContainerCmd
}

func (cmd *containerSnapshotDestroyCmd) Execute(args []string) error {
	return nil
}

type containerSnapshotListCmd struct {
	existingContainerCmd
}

func (cmd *containerSnapshotListCmd) Execute(args []string) error {
	return nil
}

type containerSnapshotRollbackCmd struct {
	existingContainerCmd
}

func (cmd *containerSnapshotRollbackCmd) Execute(args []string) error {
	return nil
}
