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
	"runtime/cgo"

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
func daos_control_fault_inject(
	handle C.uintptr_t,
	poolUUID *C.uuid_t,
	mgmtSvc C.int,
	fault *C.char,
) C.int {
	if handle == 0 {
		return C.int(errorToRC(control.ErrNoConfigFile))
	}

	ctx := cgo.Handle(handle).Value().(*ctrlContext)

	goUUID := uuidFromC(poolUUID).String()
	goFault := goString(fault)

	var rpcFn func(context.Context, *grpc.ClientConn) (proto.Message, error)
	cls, err := parseFaultClass(goFault)
	if err != nil {
		return C.int(errorToRC(err))
	}

	if mgmtSvc != 0 {
		rpcFn = func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
			return mgmtpb.NewMgmtSvcClient(conn).FaultInjectMgmtPoolFault(ctx,
				&chkpb.Fault{
					Class:   cls,
					Strings: []string{goUUID},
				},
			)
		}
	} else {
		rpcFn = func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
			return mgmtpb.NewMgmtSvcClient(conn).FaultInjectPoolFault(ctx,
				&chkpb.Fault{
					Class:   cls,
					Strings: []string{goUUID},
				},
			)
		}
	}

	_, err = control.InvokeFaultRPC(ctx.ctx(), ctx.client, rpcFn)
	return C.int(errorToRC(err))
}
