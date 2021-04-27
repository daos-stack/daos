//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"unsafe"

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

type containerSnapshotCreateCmd struct {
	existingContainerCmd

	Epoch uint64 `long:"epoch" short:"e" description:"epoch to use for snapshot"`
	Name  string `long:"name" short:"s" description:"snapshot name"`
}

func (cmd *containerSnapshotCreateCmd) Execute(args []string) error {
	cleanup, err := cmd.resolveAndConnect()
	if err != nil {
		return err
	}
	defer cleanup()

	ap, deallocCmdArgs, err := allocCmdArgs(cmd.log)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	ap.cont = cmd.cContHandle
	if err := copyUUID(&ap.c_uuid, cmd.contUUID); err != nil {
		return err
	}

	if cmd.Epoch > 0 {
		ap.epc = C.uint64_t(cmd.Epoch)
	}
	if cmd.Name != "" {
		ap.snapname_str = C.CString(cmd.Name)
		defer C.free(unsafe.Pointer(ap.snapname_str))
	}

	rc := C.cont_create_snap_hdlr(ap)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err, "failed to create snapshot of container %s",
			cmd.contUUID)
	}

	cmd.log.Infof("snapshot/epoch %d has been created", ap.epc)

	return nil
}

type containerSnapshotDestroyCmd struct {
	existingContainerCmd

	Epoch uint64 `long:"epoch" short:"e" description:"snapshot epoch to delete"`
	Range string `long:"range" short:"r" description:"range of snapshot epochs to delete"`
}

func (cmd *containerSnapshotDestroyCmd) Execute(args []string) error {
	cleanup, err := cmd.resolveAndConnect()
	if err != nil {
		return err
	}
	defer cleanup()

	ap, deallocCmdArgs, err := allocCmdArgs(cmd.log)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	ap.cont = cmd.cContHandle
	if err := copyUUID(&ap.c_uuid, cmd.contUUID); err != nil {
		return err
	}

	if cmd.Epoch > 0 {
		ap.epc = C.uint64_t(cmd.Epoch)
	}
	if cmd.Range != "" {
		var begin uint64
		var end uint64
		_, err = fmt.Sscanf(cmd.Range, "%d-%d", &begin, &end)
		if err != nil {
			return errors.Wrapf(err,
				"failed to parse range %q (must be in A-B format)",
				cmd.Range)
		}
	}

	rc := C.cont_destroy_snap_hdlr(ap)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err, "failed to destroy snapshot of container %s",
			cmd.contUUID)
	}

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
