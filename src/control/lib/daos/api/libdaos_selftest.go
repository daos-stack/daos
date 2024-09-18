//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build !test_stubs
// +build !test_stubs

package api

/*
#cgo CFLAGS: -I${SRCDIR}/../../../../cart/utils -I${SRCDIR}/../../../../utils/self_test
#cgo LDFLAGS: -lgurt -lcart -ldaos_self_test

#include "self_test_lib.h"
*/
import "C"

func run_self_test(sizes *C.struct_st_size_params, numSizes C.int, repCount C.int, maxInflight C.int, groupName *C.char,
	optMsEndpoints *C.struct_st_endpoint, numOptMsEndpoints C.uint32_t,
	tgtEndpoints *C.struct_st_endpoint, numTgtEndpoints C.uint32_t,
	msEndpoints **C.struct_st_master_endpt, numMsEndpoints *C.uint32_t,
	sizeLatencies ****C.struct_st_latency, bufAlignment C.int16_t) C.int {
	return C.run_self_test(sizes, numSizes, repCount, maxInflight, groupName,
		optMsEndpoints, numOptMsEndpoints, tgtEndpoints, numTgtEndpoints,
		msEndpoints, numMsEndpoints, sizeLatencies, bufAlignment, nil, true, true)
}

func self_test_fini(agent_used C.bool) {
	C.self_test_fini(agent_used)
}
