//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"github.com/pkg/errors"
)

/*
#include "util.h"
*/
import "C"

type containerSnapshotCreateCmd struct {
	existingContainerCmd

	Epoch uint64 `long:"epc" short:"e" description:"epoch to use for snapshot"`
	Name  string `long:"snap" short:"s" description:"snapshot name"`
}

func (cmd *containerSnapshotCreateCmd) Execute(args []string) error {
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
	if cmd.Name != "" {
		ap.snapname_str = C.CString(cmd.Name)
		defer freeString(ap.snapname_str)
	}

	rc := C.cont_create_snap_hdlr(ap)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err, "failed to create snapshot of container %s",
			cmd.contUUID)
	}

	return nil
}

type containerSnapshotDestroyCmd struct {
	existingContainerCmd

	Epoch      uint64         `long:"epc" short:"e" description:"snapshot epoch to delete"`
	EpochRange EpochRangeFlag `long:"epcrange" short:"r" description:"range of snapshot epochs to delete"`
}

func (cmd *containerSnapshotDestroyCmd) Execute(args []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.log)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(nil)
	if err != nil {
		return err
	}
	defer cleanup()

	if cmd.Epoch > 0 {
		ap.epc = C.uint64_t(cmd.Epoch)
	}
	if cmd.EpochRange.Set {
		ap.epcrange_begin = cmd.EpochRange.Begin
		ap.epcrange_end = cmd.EpochRange.End
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

	rc := C.cont_list_snaps_hdlr(ap)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err,
			"failed to list snapshots for container %s",
			cmd.contUUID)
	}

	return nil
}

type containerSnapshotRollbackCmd struct {
	existingContainerCmd

	Epoch uint64 `long:"epc" short:"e" description:"epoch to use for snapshot"`
	Name  string `long:"snap" short:"s" description:"snapshot name"`
}

func (cmd *containerSnapshotRollbackCmd) Execute(args []string) error {
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
	if cmd.Name != "" {
		ap.snapname_str = C.CString(cmd.Name)
		defer freeString(ap.snapname_str)
	}

	rc := C.cont_rollback_hdlr(ap)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err, "failed to roll back container %s",
			cmd.contUUID)
	}

	return nil
}
