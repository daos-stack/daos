//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/daos"
)

/*
#include "util.h"
*/
import "C"

type containerSnapCreateCmd struct {
	existingContainerCmd

	Name string `long:"snap" short:"s" description:"snapshot name"`
}

func (cmd *containerSnapCreateCmd) Execute(args []string) error {
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

type containerSnapDestroyCmd struct {
	existingContainerCmd

	Epoch      EpochFlag      `long:"epc" short:"e" description:"snapshot epoch to delete"`
	EpochRange EpochRangeFlag `long:"epcrange" short:"r" description:"range of snapshot epochs to delete"`
	Name       string         `long:"snap" short:"s" description:"snapshot name"`
}

func (cmd *containerSnapDestroyCmd) Execute(args []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	switch {
	case cmd.Name != "":
		if cmd.EpochRange.Set {
			return errors.New("cannot specify both snapshot name and epoch range")
		}
		if cmd.Epoch.Set {
			return errors.New("cannot specify both snapshot name and epoch")
		}

		ap.snapname_str = C.CString(cmd.Name)
		defer freeString(ap.snapname_str)
	case cmd.Epoch.Set:
		if cmd.EpochRange.Set {
			return errors.New("cannot specify both snapshot epoch and epoch range")
		}
		if cmd.Name != "" {
			return errors.New("cannot specify both snapshot epoch and name")
		}

		ap.epc = C.uint64_t(cmd.Epoch.Value)
	case cmd.EpochRange.Set:
		if cmd.Name != "" {
			return errors.New("cannot specify both snapshot epoch range and name")
		}
		if cmd.Epoch.Set {
			return errors.New("cannot specify both snapshot epoch range and epoch")
		}

		ap.epcrange_begin = cmd.EpochRange.Begin
		ap.epcrange_end = cmd.EpochRange.End
	default:
		return errors.New("must specify one of snapshot name or epoch or epoch range")
	}

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RW, ap)
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

type containerSnapListCmd struct {
	existingContainerCmd
}

func (cmd *containerSnapListCmd) Execute(args []string) error {
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

	var anchor C.daos_anchor_t
	var snapCount C.int

	rc := C.daos_cont_list_snap(ap.cont, &snapCount, nil, nil, &anchor, nil)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err, "failed to count snapshots for container %s", cmd.ContainerID())
	}

	if !C.daos_anchor_is_eof(&anchor) {
		return errors.New("too many snapshots returned")
	}

	epochs := make([]C.daos_epoch_t, snapCount)
	names := make([]*C.char, snapCount)
	defer func() {
		for _, name := range names {
			freeDaosStr(name)
		}
	}()
	expectedSnaps := snapCount
	C.memset(unsafe.Pointer(&anchor), 0, C.sizeof_daos_anchor_t)
	if snapCount > 0 {
		rc = C.daos_cont_list_snap(ap.cont, &snapCount, &epochs[0], &names[0], &anchor, nil)
		if err := daosError(rc); err != nil {
			return errors.Wrapf(err, "failed to list snapshots for container %s", cmd.ContainerID())
		}
	}

	if expectedSnaps < snapCount {
		return errors.New("snapshot list has been truncated (size changed)")
	}
	actualSnaps := func() int {
		if snapCount < expectedSnaps {
			return int(snapCount)
		} else {
			return int(expectedSnaps)
		}
	}()

	if cmd.jsonOutputEnabled() {
		type snapshot struct {
			Epoch     uint64 `json:"epoch"`
			Timestamp string `json:"timestamp"`
			Name      string `json:"name,omitempty"`
		}

		snaps := make([]*snapshot, actualSnaps)
		for i := 0; i < actualSnaps; i++ {
			snaps[i] = &snapshot{
				Epoch:     uint64(epochs[i]),
				Timestamp: daos.HLC(epochs[i]).String(),
				Name:      C.GoString(names[i]),
			}
		}

		return cmd.outputJSON(snaps, nil)
	}

	cmd.Info("Container's snapshots :")
	if snapCount == 0 {
		cmd.Info("no snapshots")
		return nil
	}
	for i := 0; i < actualSnaps; i++ {
		cmd.Infof("0x%x %s", epochs[i], C.GoString(names[i]))
	}

	return nil
}

type containerSnapshotRollbackCmd struct {
	existingContainerCmd

	Epoch EpochFlag `long:"epc" short:"e" description:"epoch to use for snapshot"`
	Name  string    `long:"snap" short:"s" description:"snapshot name"`
}

func (cmd *containerSnapshotRollbackCmd) Execute(args []string) error {
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

	if cmd.Epoch.Set {
		ap.epc = C.uint64_t(cmd.Epoch.Value)
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
