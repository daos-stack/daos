//
// (C) Copyright 2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.

// libdaos_control provides a C-compatible wrapper around
// the lib/control package.
//
// NB: This library is not suitable for multi-threaded applications!
package main

/*
#cgo LDFLAGS: -ldaos -lgurt
#include <daos.h>
#include <gurt/common.h>
*/
import "C"
import (
	"context"
	"fmt"
	"os"
	"unsafe"

	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/lib/control"
)

// daosControlCtx holds the context and gRPC invoker
// created by daos_control_init.
type daosControlCtx struct {
	ctx        context.Context
	rpcInvoker control.Invoker
}

var (
	// singleton created by daos_control_init
	dcc *daosControlCtx
)

// only needed to make the compiler happy
func main() {}

// daos_control_init is responsible for configuring a gRPC
// invoker and calling daos_init().
//
//export daos_control_init
func daos_control_init(config *C.char) C.int {
	if dcc != nil {
		return -C.DER_INVAL
	}

	if rc := C.daos_init(); rc != 0 {
		return rc
	}

	cfg, err := control.LoadConfig(C.GoString(config))
	if err != nil {
		return -C.DER_INVAL
	}

	dcc = &daosControlCtx{
		ctx: context.Background(),
		rpcInvoker: control.NewClient(
			control.WithConfig(cfg),
		),
	}

	return 0
}

// daos_control_fini is responsible for cleaning up
// any connections to DAOS before exiting.
//
//export daos_control_fini
func daos_control_fini() C.int {
	if dcc == nil {
		return -C.DER_INVAL
	}
	dcc = nil

	return C.daos_fini()
}

// daos_control_list_pools is a wrapper around control.ListPools() and
// provides compatibility with daos_mgmt_list_pools(). Caller is responsible
// for freeing svc rank lists.
//
//export daos_control_list_pools
func daos_control_list_pools(cGroup *C.char, cNpools *C.daos_size_t,
	cPools *C.daos_mgmt_pool_info_t, cEv *C.daos_event_t) C.int {
	if dcc == nil {
		return -C.DER_INVAL
	}

	req := &control.ListPoolsReq{
		System: C.GoString(cGroup),
	}

	resp, err := control.ListPools(dcc.ctx, dcc.rpcInvoker, req)
	if err != nil {
		fmt.Fprintf(os.Stderr, "ListPools err: %s\n", err)
		return -C.DER_INVAL
	}

	if cNpools == nil {
		return -C.DER_INVAL
	}
	nPools := len(resp.Pools)
	*cNpools = C.daos_size_t(nPools)

	if cPools == nil {
		return 0
	}

	cPoolSlice := (*[1 << 30]C.daos_mgmt_pool_info_t)(unsafe.Pointer(cPools))[:nPools:nPools]
	for i, pool := range resp.Pools {
		pUUID, err := uuid.Parse(pool.UUID)
		if err != nil {
			fmt.Fprintf(os.Stderr, "bad UUID: %s", pool.UUID)
			return -C.DER_INVAL
		}
		uuidBytes, err := pUUID.MarshalBinary()
		if err != nil {
			return -C.DER_INVAL
		}
		for j, ub := range uuidBytes {
			cPoolSlice[i].mgpi_uuid[j] = C.uchar(ub)
		}

		nRanks := len(pool.SvcReplicas)
		if nRanks > 0 {
			cPoolSlice[i].mgpi_svc = C.d_rank_list_alloc(C.uint(nRanks))
			rankSlice := (*[1 << 30]C.uint32_t)(unsafe.Pointer(cPoolSlice[i].mgpi_svc.rl_ranks))[:nRanks:nRanks]
			for j, rank := range pool.SvcReplicas {
				rankSlice[j] = C.uint32_t(rank)
			}
		}
	}

	return 0
}
