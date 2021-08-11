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

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RW, ap)
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
	Name       string         `long:"snap" short:"s" description:"snapshot name"`
}

func (cmd *containerSnapshotDestroyCmd) Execute(args []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.log)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	switch {
	case cmd.Name != "":
		if cmd.EpochRange.Set {
			return errors.New("cannot specify both snapshot name and epoch range")
		}
		if cmd.Epoch > 0 {
			return errors.New("cannot specify both snapshot name and epoch")
		}

		ap.snapname_str = C.CString(cmd.Name)
		defer freeString(ap.snapname_str)
	case cmd.Epoch > 0:
		if cmd.EpochRange.Set {
			return errors.New("cannot specify both snapshot epoch and epoch range")
		}
		if cmd.Name != "" {
			return errors.New("cannot specify both snapshot epoch and name")
		}

		ap.epc = C.uint64_t(cmd.Epoch)
	case cmd.EpochRange.Set:
		if cmd.Name != "" {
			return errors.New("cannot specify both snapshot epoch range and name")
		}
		if cmd.Epoch > 0 {
			return errors.New("cannot specify both snapshot epoch range and epoch")
		}

		ap.epcrange_begin = cmd.EpochRange.Begin
		ap.epcrange_end = cmd.EpochRange.End
	default:
		return errors.New("must specify one of snapshot name or epoch or epoch range")
	}

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RW, nil)
	if err != nil {
		return err
	}
	defer cleanup()

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

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RO, ap)
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

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RW, ap)
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
