//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build test_stubs
// +build test_stubs

package api

import (
	"unsafe"

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

/*
#cgo CFLAGS: -I${SRCDIR}/../../../../cart/utils -I${SRCDIR}/../../../../utils/self_test
#cgo LDFLAGS: -lgurt -lcart -ldaos_self_test

#include <daos_errno.h>
#include <gurt/common.h>

#include "self_test_lib.h"

struct st_latency ***
alloc_latency_arrays(size_t num_sizes, size_t num_endpoints, size_t num_latencies)
{
	struct st_latency	***latencies = NULL;
	int					   i, j;

	D_ALLOC_ARRAY(latencies, num_sizes);
	if (latencies == NULL)
		return NULL;

	for (i = 0; i < num_sizes; i++) {
		D_ALLOC_ARRAY(latencies[i], num_endpoints);
		if (latencies[i] == NULL)
			return NULL;

		for (j = 0; j < num_endpoints; j++) {
			D_ALLOC_ARRAY(latencies[i][j], num_latencies);
			if (latencies[i][j] == NULL)
				return NULL;
		}
	}

	return latencies;
}
*/
import "C"

type run_self_test_EndpointLatency struct {
	val    C.int64_t
	rank   C.uint32_t
	tag    C.uint32_t
	cci_rc C.int
}

var (
	run_self_test_RunConfig         *daos.SelfTestConfig
	run_self_test_RC                C.int = 0
	run_self_test_MsEndpoints       []daos.SelfTestEndpoint
	run_self_test_EndpointLatencies []run_self_test_EndpointLatency
)

func run_self_test(sizes *C.struct_st_size_params, numSizes C.int, repCount C.int, maxInflight C.int, groupName *C.char,
	optMsEndpoints *C.struct_st_endpoint, numOptMsEndpoints C.uint32_t,
	tgtEndpoints *C.struct_st_endpoint, numTgtEndpoints C.uint32_t,
	msEndpoints **C.struct_st_master_endpt, numMsEndpoints *C.uint32_t,
	sizeLatencies ****C.struct_st_latency, bufAlignment C.int16_t) C.int {

	cfg := &daos.SelfTestConfig{
		GroupName:       C.GoString(groupName),
		Repetitions:     uint(repCount),
		BufferAlignment: int16(bufAlignment),
		MaxInflightRPCs: uint(maxInflight),
	}

	if numSizes > 0 {
		cfg.SendSizes = make([]uint64, int(numSizes))
		cfg.ReplySizes = make([]uint64, int(numSizes))
		testSizesSlice := unsafe.Slice(sizes, int(numSizes))
		for i := 0; i < int(numSizes); i++ {
			cfg.SendSizes[i] = uint64(testSizesSlice[i].send_size)
			cfg.ReplySizes[i] = uint64(testSizesSlice[i].reply_size)
		}
	}

	if numOptMsEndpoints > 0 {
		cfg.MasterEndpoints = make([]daos.SelfTestEndpoint, int(numOptMsEndpoints))
		msEndpointsSlice := unsafe.Slice(optMsEndpoints, int(numOptMsEndpoints))
		for i := 0; i < int(numOptMsEndpoints); i++ {
			cfg.MasterEndpoints[i].Rank = ranklist.Rank(msEndpointsSlice[i].rank)
			cfg.MasterEndpoints[i].Tag = uint32(msEndpointsSlice[i].tag)
		}
		run_self_test_MsEndpoints = cfg.MasterEndpoints
	}

	if numTgtEndpoints > 0 {
		rankSet := ranklist.NewRankSet()
		tagSet := hostlist.NewNumericSet()
		tgtEndpointsSlice := unsafe.Slice(tgtEndpoints, int(numTgtEndpoints))
		for i := 0; i < int(numTgtEndpoints); i++ {
			rankSet.Add(ranklist.Rank(tgtEndpointsSlice[i].rank))
			tagSet.Add(uint(tgtEndpointsSlice[i].tag))
		}
		cfg.EndpointRanks = rankSet.Ranks()
		for _, tag := range tagSet.Slice() {
			cfg.EndpointTags = append(cfg.EndpointTags, uint32(tag))
		}

		// If the configuration doesn't specify master endpoints,
		// create one similarly to how the library does it.
		if len(run_self_test_MsEndpoints) == 0 {
			run_self_test_MsEndpoints = []daos.SelfTestEndpoint{
				{
					Rank: cfg.EndpointRanks[len(cfg.EndpointRanks)-1] + 1,
					Tag:  cfg.EndpointTags[len(cfg.EndpointTags)-1],
				},
			}
		}
	}

	run_self_test_RunConfig = cfg
	if run_self_test_RC != 0 {
		return run_self_test_RC
	}

	// Construct the C array of master endpoints for the out parameter.
	// Must be freed by the caller.
	*numMsEndpoints = C.uint32_t(len(run_self_test_MsEndpoints))
	ptr, err := C.calloc(C.size_t(len(run_self_test_MsEndpoints)), C.sizeof_struct_st_master_endpt)
	if err != nil {
		panic("calloc() failed for master endpoints")
	}
	*msEndpoints = (*C.struct_st_master_endpt)(ptr)
	msEpSlice := unsafe.Slice(*msEndpoints, int(*numMsEndpoints))
	for i := 0; i < int(*numMsEndpoints); i++ {
		msEpSlice[i].endpt.ep_rank = C.uint32_t(run_self_test_MsEndpoints[i].Rank)
		msEpSlice[i].endpt.ep_tag = C.uint32_t(run_self_test_MsEndpoints[i].Tag)
	}

	// Construct the multi-dimensional C array of test latencies for the out parameter.
	// Must be freed by the caller.
	*sizeLatencies = C.alloc_latency_arrays(C.size_t(numSizes), C.size_t(*numMsEndpoints), C.size_t(len(run_self_test_EndpointLatencies)))
	if *sizeLatencies == nil {
		panic("calloc() failed for latency arrays")
	}

	sizesSlice := unsafe.Slice(*sizeLatencies, int(numSizes))
	for i := 0; i < int(numSizes); i++ {
		msSessSlice := unsafe.Slice(sizesSlice[i], int(*numMsEndpoints))
		for j := 0; j < int(*numMsEndpoints); j++ {
			epLatSlice := unsafe.Slice(msSessSlice[j], len(run_self_test_EndpointLatencies))

			for k := 0; k < len(run_self_test_EndpointLatencies); k++ {
				epLatSlice[k].val = run_self_test_EndpointLatencies[k].val
				epLatSlice[k].rank = run_self_test_EndpointLatencies[k].rank
				epLatSlice[k].tag = run_self_test_EndpointLatencies[k].tag
			}
		}
	}

	return run_self_test_RC
}

func self_test_fini(agent_used C.bool) {}
