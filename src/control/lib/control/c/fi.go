//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build fault_injection

package main

/*
#include <stdlib.h>
#include <stdint.h>
#include <uuid/uuid.h>
*/
import "C"
import (
	"context"

	"google.golang.org/grpc"
	"google.golang.org/protobuf/proto"

	chkpb "github.com/daos-stack/daos/src/control/common/proto/chk"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/control"
)

func parseFaultClass(name string) (chkpb.CheckInconsistClass, error) {
	var cls control.SystemCheckFindingClass
	if err := cls.FromString(name); err != nil {
		return chkpb.CheckInconsistClass_CIC_NONE, err
	}
	return chkpb.CheckInconsistClass(cls), nil
}

//export daos_control_fault_inject
func daos_control_fault_inject(handle C.uintptr_t, poolUUID *C.uuid_t, mgmtSvc C.int, fault *C.char) C.int {
	return withContext(handle, func(ctx *ctrlContext) error {
		cls, err := parseFaultClass(goString(fault))
		if err != nil {
			return err
		}
		goUUID := uuidFromC(poolUUID).String()
		payload := &chkpb.Fault{Class: cls, Strings: []string{goUUID}}

		rpcFn := func(rpcCtx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
			if mgmtSvc != 0 {
				return mgmtpb.NewMgmtSvcClient(conn).FaultInjectMgmtPoolFault(rpcCtx, payload)
			}
			return mgmtpb.NewMgmtSvcClient(conn).FaultInjectPoolFault(rpcCtx, payload)
		}
		_, err = control.InvokeFaultRPC(ctx.ctx(), ctx.client, rpcFn)
		return err
	})
}
