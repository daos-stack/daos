//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package api

import (
	"context"
	"time"
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
)

/*
#cgo CFLAGS: -I${SRCDIR}/../../../../cart/utils -I${SRCDIR}/../../../../utils/self_test

#include "self_test_lib.h"

void
set_size_params(struct st_size_params *params, int send_size, int reply_size)
{
	params->send_size = send_size;
	if (send_size == 0)
		params->send_type = CRT_SELF_TEST_MSG_TYPE_EMPTY;
	else if (send_size < CRT_SELF_TEST_AUTO_BULK_THRESH)
		params->send_type = CRT_SELF_TEST_MSG_TYPE_IOV;
	else
		params->send_type = CRT_SELF_TEST_MSG_TYPE_BULK_GET;

	params->reply_size = reply_size;
	if (reply_size == 0)
		params->reply_type = CRT_SELF_TEST_MSG_TYPE_EMPTY;
	else if (reply_size < CRT_SELF_TEST_AUTO_BULK_THRESH)
		params->reply_type = CRT_SELF_TEST_MSG_TYPE_IOV;
	else
		params->reply_type = CRT_SELF_TEST_MSG_TYPE_BULK_PUT;
}
*/
import "C"

type tgtEndpointSlice []daos.SelfTestEndpoint

// toC returns a pointer to a C array of target endpoints.
// NB: Caller must free the array when finished.
func (tes tgtEndpointSlice) toC() (*C.struct_st_endpoint, error) {
	if len(tes) == 0 {
		return nil, errors.New("empty tgt endpoint slice")
	}

	ptr, err := C.calloc(C.size_t(len(tes)), C.sizeof_struct_st_endpoint)
	if err != nil {
		return nil, err
	}
	cEndpoints := (*C.struct_st_endpoint)(ptr)
	endPoints := unsafe.Slice(cEndpoints, len(tes))
	for i, ep := range tes {
		endPoints[i].rank = C.uint32_t(ep.Rank)
		endPoints[i].tag = C.uint32_t(ep.Tag)
	}

	return cEndpoints, nil
}

// getAllSystemRanks returns the set of system ranks available to use
// for a self_test run. If no ranks are available, a sentinel error
// is returned.
func getAllSystemRanks(ctx context.Context) ([]ranklist.Rank, error) {
	log := logging.FromContext(ctx)
	p, err := NewProvider(log, false)
	if err != nil {
		return nil, err
	}
	defer p.Cleanup()

	si, err := p.GetSystemInfo(ctx)
	if err != nil {
		return nil, err
	}

	if len(si.RankURIs) == 0 {
		return nil, ErrNoSystemRanks
	}

	var systemRanks []ranklist.Rank
	for _, rankURI := range si.RankURIs {
		systemRanks = append(systemRanks, ranklist.Rank(rankURI.Rank))
	}

	return systemRanks, nil
}

// RunSelfTest uses the provided configuration to run the logic
// behind the self_test tool. Per-size structured test results
// are returned as a slice.
func RunSelfTest(ctx context.Context, cfg *daos.SelfTestConfig) ([]*daos.SelfTestResult, error) {
	if err := cfg.Validate(); err != nil {
		return nil, errors.Wrap(err, "invalid self_test configuration")
	}

	ptr, err := C.calloc(C.size_t(len(cfg.SendSizes)), C.sizeof_struct_st_size_params)
	if err != nil {
		return nil, err
	}
	cSizes := (*C.struct_st_size_params)(ptr)
	defer C.free(unsafe.Pointer(cSizes))
	testSizes := unsafe.Slice(cSizes, len(cfg.SendSizes))
	for i := 0; i < len(testSizes); i++ {
		C.set_size_params(&testSizes[i], C.int(cfg.SendSizes[i]), C.int(cfg.ReplySizes[i]))
	}

	if len(cfg.EndpointRanks) == 0 {
		cfg.EndpointRanks, err = getAllSystemRanks(ctx)
		if err != nil {
			return nil, err
		}
	}

	tgtEndpoints := make(tgtEndpointSlice, 0, len(cfg.EndpointRanks)*len(cfg.EndpointTags))
	for _, r := range cfg.EndpointRanks {
		for _, t := range cfg.EndpointTags {
			tgtEndpoints = append(tgtEndpoints, daos.SelfTestEndpoint{Rank: r, Tag: t})
		}
	}
	cTgtEndpoints, err := tgtEndpoints.toC()
	defer C.free(unsafe.Pointer(cTgtEndpoints))

	repCount := C.int(int(cfg.Repetitions) * len(tgtEndpoints))
	maxInflight := C.int(cfg.MaxInflightRPCs)
	var cOptMasterEndpoints *C.struct_st_endpoint
	var numOptMsEndpoints C.uint
	var cMasterEndpoints *C.struct_st_master_endpt
	var numMsEndpoints C.uint32_t
	var cSizeLatencies ***C.struct_st_latency
	var bufAlignment = C.int16_t(cfg.BufferAlignment)

	cGroupName := C.CString(cfg.GroupName)
	defer C.free(unsafe.Pointer(cGroupName))

	if len(cfg.MasterEndpoints) > 0 {
		numOptMsEndpoints = C.uint(len(cfg.MasterEndpoints))
		ptr, err := C.calloc(C.size_t(numOptMsEndpoints), C.sizeof_struct_st_endpoint)
		if err != nil {
			return nil, err
		}
		cOptMasterEndpoints = (*C.struct_st_endpoint)(ptr)
		defer C.free(unsafe.Pointer(cOptMasterEndpoints))

		masterEndpoints := unsafe.Slice(cOptMasterEndpoints, int(numOptMsEndpoints))
		for i, ep := range cfg.MasterEndpoints {
			masterEndpoints[i].rank = C.uint(ep.Rank)
			masterEndpoints[i].tag = C.uint(ep.Tag)
		}
	}

	defer func() {
		if cMasterEndpoints != nil {
			C.free(unsafe.Pointer(cMasterEndpoints))
		}
		if cSizeLatencies != nil {
			C.free_size_latencies(cSizeLatencies, C.uint32_t(len(testSizes)), numMsEndpoints)
		}
		self_test_fini(true)
	}()

	rc := run_self_test(cSizes, C.int(len(testSizes)), repCount, maxInflight, cGroupName,
		cOptMasterEndpoints, numOptMsEndpoints,
		cTgtEndpoints, C.uint32_t(len(tgtEndpoints)),
		&cMasterEndpoints, &numMsEndpoints,
		&cSizeLatencies, bufAlignment)
	if err := daos.ErrorFromRC(int(rc)); err != nil {
		return nil, errors.Wrap(err, "self_test failed")
	}

	if numMsEndpoints == 0 || cMasterEndpoints == nil {
		return nil, errors.New("no master endpoints defined")
	}
	if cSizeLatencies == nil {
		return nil, errors.New("no test latencies recorded")
	}

	masterEndpoints := unsafe.Slice(cMasterEndpoints, int(numMsEndpoints))
	var results []*daos.SelfTestResult
	perSizeList := unsafe.Slice(cSizeLatencies, len(testSizes))
	for i := 0; i < len(testSizes); i++ {
		params := testSizes[i]
		msSessions := unsafe.Slice(perSizeList[i], int(numMsEndpoints))
		for j := 0; j < int(numMsEndpoints); j++ {
			msEp := masterEndpoints[j]
			res := &daos.SelfTestResult{
				MasterEndpoint: daos.SelfTestEndpoint{
					Rank: ranklist.Rank(msEp.endpt.ep_rank),
					Tag:  uint32(msEp.endpt.ep_tag),
				},
				TargetEndpoints: tgtEndpoints,
				Repetitions:     uint(repCount),
				SendSize:        uint64(params.send_size),
				ReplySize:       uint64(params.reply_size),
				BufferAlignment: int16(bufAlignment),
				Duration:        time.Duration(msEp.reply.test_duration_ns),
				MasterLatency:   new(daos.EndpointLatency),
				TargetLatencies: make(map[daos.SelfTestEndpoint]*daos.EndpointLatency),
			}
			repLatencies := unsafe.Slice(msSessions[j], int(repCount))

			for _, latency := range repLatencies {
				if latency.cci_rc < 0 {
					res.MasterLatency.AddValue(-1)
					res.AddTargetLatency(ranklist.Rank(latency.rank), uint32(latency.tag), -1)
					continue
				}
				res.MasterLatency.AddValue(int64(latency.val))
				res.AddTargetLatency(ranklist.Rank(latency.rank), uint32(latency.tag), int64(latency.val))
			}

			results = append(results, res)
		}
	}

	return results, nil
}
