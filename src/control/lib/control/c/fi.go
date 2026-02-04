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
	if mgmtSvc != 0 {
		rpcFn = func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
			req := &chkpb.Fault{
				Strings: []string{goUUID},
			}
			if err := req.Class.UnmarshalJSON([]byte(`"` + goFault + `"`)); err != nil {
				// Try with CIC_ prefix
				if err2 := req.Class.UnmarshalJSON([]byte(`"CIC_` + goFault + `"`)); err2 != nil {
					return nil, err
				}
			}
			return mgmtpb.NewMgmtSvcClient(conn).FaultInjectMgmtPoolFault(ctx, req)
		}
	} else {
		rpcFn = func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
			req := &chkpb.Fault{
				Strings: []string{goUUID},
			}
			if err := req.Class.UnmarshalJSON([]byte(`"` + goFault + `"`)); err != nil {
				// Try with CIC_ prefix
				if err2 := req.Class.UnmarshalJSON([]byte(`"CIC_` + goFault + `"`)); err2 != nil {
					return nil, err
				}
			}
			return mgmtpb.NewMgmtSvcClient(conn).FaultInjectPoolFault(ctx, req)
		}
	}

	_, err := control.InvokeFaultRPC(ctx.ctx(), ctx.client, rpcFn)
	return C.int(errorToRC(err))
}
