//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/daos"
)

/*
#include "util.h"
*/
import "C"

type snapshot struct {
	Epoch     uint64 `json:"epoch"`
	Timestamp string `json:"timestamp"`
	Name      string `json:"name,omitempty"`
}

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

	var cSnapName *C.char
	if cmd.Name != "" {
		cSnapName = C.CString(cmd.Name)
		defer freeString(cSnapName)
	}

	var cEpoch C.uint64_t
	if err := daosError(C.daos_cont_create_snap(ap.cont, &cEpoch, cSnapName, nil)); err != nil {
		return errors.Wrapf(err, "failed to create snapshot of container %s", cmd.ContainerID())
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(snapshot{
			Epoch:     uint64(cEpoch),
			Timestamp: common.FormatTime(daos.HLC(cEpoch).ToTime()),
			Name:      cmd.Name,
		}, nil)
	}

	cmd.Infof("snapshot/epoch 0x%x has been created", cEpoch)

	return nil
}

func resolveSnapName(ap *C.struct_cmd_args_s, containerID, name string) (C.uint64_t, error) {
	snaps, err := listContainerSnapshots(ap, containerID)
	if err != nil {
		return 0, err
	}

	for _, snap := range snaps {
		if snap.Name == name {
			return C.uint64_t(snap.Epoch), nil
		}
	}

	return 0, errors.Errorf("snapshot %q not found", name)
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

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RW, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	var epr C.daos_epoch_range_t
	switch {
	case cmd.Name != "":
		if cmd.EpochRange.Set {
			return errors.New("cannot specify both snapshot name and epoch range")
		}
		if cmd.Epoch.Set {
			return errors.New("cannot specify both snapshot name and epoch")
		}

		epc, err := resolveSnapName(ap, cmd.ContainerID().String(), cmd.Name)
		if err != nil {
			return err
		}

		epr.epr_lo = epc
		epr.epr_hi = epc
	case cmd.Epoch.Set:
		if cmd.EpochRange.Set {
			return errors.New("cannot specify both snapshot epoch and epoch range")
		}
		if cmd.Name != "" {
			return errors.New("cannot specify both snapshot epoch and name")
		}

		epr.epr_lo = C.uint64_t(cmd.Epoch.Value)
		epr.epr_hi = epr.epr_lo
	case cmd.EpochRange.Set:
		if cmd.Name != "" {
			return errors.New("cannot specify both snapshot epoch range and name")
		}
		if cmd.Epoch.Set {
			return errors.New("cannot specify both snapshot epoch range and epoch")
		}

		epr.epr_lo = C.uint64_t(cmd.EpochRange.Begin)
		epr.epr_hi = C.uint64_t(cmd.EpochRange.End)
	default:
		return errors.New("must specify one of snapshot name or epoch or epoch range")
	}

	if err := daosError(C.daos_cont_destroy_snap(ap.cont, epr, nil)); err != nil {
		return errors.Wrapf(err, "failed to destroy snapshots of container %s", cmd.ContainerID())
	}

	return nil
}

type containerSnapListCmd struct {
	existingContainerCmd
}

func listContainerSnapshots(ap *C.struct_cmd_args_s, containerID string) ([]*snapshot, error) {
	var anchor C.daos_anchor_t
	var snapCount C.int

	rc := C.daos_cont_list_snap(ap.cont, &snapCount, nil, nil, &anchor, nil)
	if err := daosError(rc); err != nil {
		return nil, errors.Wrapf(err, "failed to count snapshots for container %s", containerID)
	}

	if !C.daos_anchor_is_eof(&anchor) {
		return nil, errors.New("too many snapshots returned")
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
			return nil, errors.Wrapf(err, "failed to list snapshots for container %s", containerID)
		}
	}

	if expectedSnaps < snapCount {
		return nil, errors.New("snapshot list has been truncated (size changed)")
	}
	actualSnaps := func() int {
		if snapCount < expectedSnaps {
			return int(snapCount)
		} else {
			return int(expectedSnaps)
		}
	}()

	snapshots := make([]*snapshot, actualSnaps)
	for i := range snapshots {
		snapshots[i] = &snapshot{
			Epoch:     uint64(epochs[i]),
			Timestamp: common.FormatTime(daos.HLC(epochs[i]).ToTime()),
		}
		if names[i] != nil {
			snapshots[i].Name = C.GoString((*C.char)(names[i]))
		}
	}

	return snapshots, nil
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

	snaps, err := listContainerSnapshots(ap, cmd.ContainerID().String())
	if err != nil {
		return err
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(snaps, nil)
	}

	cmd.Info("Container's snapshots :")
	if len(snaps) == 0 {
		cmd.Info("no snapshots")
		return nil
	}
	for _, snap := range snaps {
		cmd.Infof("0x%x %s", snap.Epoch, snap.Name)
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

	var epc C.uint64_t
	if cmd.Epoch.Set {
		if cmd.Name != "" {
			return errors.New("cannot specify both snapshot epoch and name")
		}
		epc = C.uint64_t(cmd.Epoch.Value)
	}
	if cmd.Name != "" {
		if cmd.Epoch.Set {
			return errors.New("cannot specify both snapshot epoch and name")
		}
		var err error
		epc, err = resolveSnapName(ap, cmd.ContainerID().String(), cmd.Name)
		if err != nil {
			return err
		}
	}
	if epc == 0 {
		return errors.New("must specify one of snapshot name or epoch")
	}

	if err := daosError(C.daos_cont_rollback(ap.cont, epc, nil)); err != nil {
		return errors.Wrapf(err, "failed to roll back container %s", cmd.ContainerID())
	}

	return nil
}
