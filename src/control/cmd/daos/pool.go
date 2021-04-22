//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"unsafe"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
)

/*
#include "daos.h"
#include "daos_types.h"
*/
import "C"

type poolBaseCmd struct {
	daosCmd
	poolUUID uuid.UUID

	cPoolHandle C.daos_handle_t

	SysName string `long:"sys-name" short:"G" description:"DAOS system name"`
	Args    struct {
		Pool string `positional-arg-name:"<pool name or UUID>"`
	} `positional-args:"yes" required:"yes"`
}

func (cmd *poolBaseCmd) poolUUIDPtr() *C.uchar {
	return (*C.uchar)(unsafe.Pointer(&cmd.poolUUID[0]))
}

func (cmd *poolBaseCmd) resolvePool() (err error) {
	// TODO: Resolve name
	cmd.poolUUID, err = uuid.Parse(cmd.Args.Pool)
	if err != nil {
		return
	}

	return
}

func (cmd *poolBaseCmd) resolveAndConnect() (func(), error) {
	if err := cmd.resolvePool(); err != nil {
		return nil, err
	}

	if err := cmd.connectPool(); err != nil {
		return nil, err
	}

	return func() {
		cmd.disconnectPool()
	}, nil
}

func (cmd *poolBaseCmd) connectPool() error {
	sysName := cmd.SysName
	if sysName == "" {
		sysName = build.DefaultSystemName
	}

	rc := C.daos_pool_connect(cmd.poolUUIDPtr(), C.CString(sysName),
		C.DAOS_PC_RW, &cmd.cPoolHandle, nil, nil)
	return daosError(rc)
}

func (cmd *poolBaseCmd) disconnectPool() {
	rc := C.daos_pool_disconnect(cmd.cPoolHandle, nil)
	if err := daosError(rc); err != nil {
		cmd.log.Errorf("pool disconnect failed: %s", err)
	}
}

type poolCmd struct {
	ListContainers poolContainersListCmd `command:"list-containers" alias:"list-cont" alias:"ls" description:"container operations for the specified pool"`
}

type poolContainersListCmd struct {
	poolBaseCmd
}

func (cmd *poolContainersListCmd) Execute(_ []string) error {
	extra_cont_margin := C.size_t(16)

	cleanup, err := cmd.resolveAndConnect()
	if err != nil {
		return err
	}
	defer cleanup()

	// First call gets the current number of containers.
	var ncont C.daos_size_t
	rc := C.daos_pool_list_cont(cmd.cPoolHandle, &ncont, nil, nil)
	if err := daosError(rc); err != nil {
		return err
	}

	// No containers.
	if ncont == 0 {
		return nil
	}

	var cConts *C.struct_daos_pool_cont_info
	// Extend ncont with a safety margin to account for containers
	// that might have been created since the first API call.
	ncont += extra_cont_margin
	cConts = (*C.struct_daos_pool_cont_info)(C.calloc(C.sizeof_struct_daos_pool_cont_info, ncont))
	if cConts == nil {
		return errors.New("malloc() failed")
	}
	defer C.free(unsafe.Pointer(cConts))

	rc = C.daos_pool_list_cont(cmd.cPoolHandle, &ncont, cConts, nil)
	if err := daosError(rc); err != nil {
		return err
	}

	conts := (*[1 << 30]C.struct_daos_pool_cont_info)(unsafe.Pointer(cConts))[:ncont:ncont]
	contUUIDs := make([]uuid.UUID, ncont)
	for i, cont := range conts {
		buf := C.GoBytes(unsafe.Pointer(&cont.pci_uuid[0]), C.int(len(cont.pci_uuid)))
		contUUIDs[i] = uuid.Must(uuid.FromBytes(buf))
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(contUUIDs, nil)
	}

	for _, cont := range contUUIDs {
		cmd.log.Infof("%s", cont)
	}

	return nil
}
