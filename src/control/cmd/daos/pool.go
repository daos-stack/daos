//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"unsafe"

	"github.com/google/uuid"

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
