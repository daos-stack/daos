//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"fmt"
	"math/rand"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/golang/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
)

const maxConcurrent = 100

type netdetectCleanup func(context.Context)

func initCache(t *testing.T, scanResults []*netdetect.FabricScan, aiCache *attachInfoCache) (context.Context, netdetectCleanup) {
	netCtx, err := netdetect.Init(context.Background())
	if err != nil {
		t.Fatalf("failed to init netdetect context: %v", err)
	}
	err = aiCache.initResponseCache(netCtx, &mgmtpb.GetAttachInfoResp{}, scanResults)
	if err != nil {
		t.Fatalf("initResponseCache error: %v", err)
	}
	return netCtx, netdetect.CleanUp
}

func TestInfoCacheInitNoScanResults(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)
	enabled := atm.NewBool(true)
	scanResults := []*netdetect.FabricScan{}
	aiCache := attachInfoCache{log: log, enabled: enabled}

	netCtx, cleanupFn := initCache(t, scanResults, &aiCache)
	defer cleanupFn(netCtx)

	common.AssertTrue(t, aiCache.isCached() == true, "initResponseCache failed to initialized")

	for name, tc := range map[string]struct {
		numaNode   int
		deviceName string
	}{
		"info cache response for numa 0": {
			numaNode:   0,
			deviceName: "eth0",
		},
		"info cache response for numa 1": {
			numaNode:   1,
			deviceName: "eth1",
		},
		"info cache response for numa 2": {
			numaNode:   2,
			deviceName: "eth2",
		},
	} {
		t.Run(name, func(t *testing.T) {
			var res []byte
			var err error
			if aiCache.isCached() {
				res, err = aiCache.getResponse(tc.numaNode)
				common.AssertEqual(t, err, nil, "getResponse error")
			}
			resp := &mgmtpb.GetAttachInfoResp{}
			if err = proto.Unmarshal(res, resp); err != nil {
				t.Errorf("Expected error on proto.Unmarshal, got %+v", err)
			}
			common.AssertTrue(t, resp.GetInterface() == defaultNetworkDevice, fmt.Sprintf("Expected default interface: %s, got %s", defaultNetworkDevice, resp.GetInterface()))
			common.AssertTrue(t, resp.GetDomain() == defaultDomain, fmt.Sprintf("Expected default domain: %s, got %s", defaultDomain, resp.GetDomain()))
		})
	}
}

func TestInfoCacheInit(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)
	enabled := atm.NewBool(true)
	scanResults := []*netdetect.FabricScan{
		{Provider: "ofi+sockets", DeviceName: "eth0_node0", NUMANode: 0},
		{Provider: "ofi+sockets", DeviceName: "eth1_node0", NUMANode: 0},
		{Provider: "ofi+sockets", DeviceName: "eth2_node0", NUMANode: 0},
		{Provider: "ofi+sockets", DeviceName: "eth3_node0", NUMANode: 0},
		{Provider: "ofi+sockets", DeviceName: "eth0_node1", NUMANode: 1},
		{Provider: "ofi+sockets", DeviceName: "eth1_node1", NUMANode: 1},
		{Provider: "ofi+sockets", DeviceName: "eth0_node2", NUMANode: 2},
		{Provider: "ofi+sockets", DeviceName: "eth1_node2", NUMANode: 2},
		{Provider: "ofi+sockets", DeviceName: "eth2_node2", NUMANode: 2},
		{Provider: "ofi+sockets", DeviceName: "eth3_node2", NUMANode: 2}}

	aiCache := attachInfoCache{log: log, enabled: enabled}

	netCtx, cleanupFn := initCache(t, scanResults, &aiCache)
	defer cleanupFn(netCtx)

	for name, tc := range map[string]struct {
		numaNode int
		numDevs  int
	}{
		"info cache response for numa 0": {
			numaNode: 0,
			numDevs:  4,
		},
		"info cache response for numa 1": {
			numaNode: 1,
			numDevs:  2,
		},
		"info cache response for numa 2": {
			numaNode: 2,
			numDevs:  4,
		},
	} {
		t.Run(name, func(t *testing.T) {

			numDevs := len(aiCache.numaDeviceMarshResp[tc.numaNode])
			common.AssertEqual(t, numDevs, tc.numDevs,
				fmt.Sprintf("initResponseCache error - expected %d cached responses, got %d", tc.numDevs, numDevs))
		})
	}
}

func TestInfoCacheInitWithDeviceFiltering(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)
	enabled := atm.NewBool(true)

	scanResults := []*netdetect.FabricScan{
		{Provider: "ofi+sockets", DeviceName: "eth0_node0", NUMANode: 0, NetDevClass: netdetect.Ether},
		{Provider: "ofi+sockets", DeviceName: "eth1_node0", NUMANode: 0, NetDevClass: netdetect.Ether},
		{Provider: "ofi+sockets", DeviceName: "eth2_node0", NUMANode: 0, NetDevClass: netdetect.Ether},
		{Provider: "ofi+sockets", DeviceName: "ib0_node0", NUMANode: 0, NetDevClass: netdetect.Infiniband},
		{Provider: "ofi+sockets", DeviceName: "eth0_node1", NUMANode: 1, NetDevClass: netdetect.Ether},
		{Provider: "ofi+sockets", DeviceName: "ib1_node1", NUMANode: 1, NetDevClass: netdetect.Infiniband},
		{Provider: "ofi+sockets", DeviceName: "eth0_node2", NUMANode: 2, NetDevClass: netdetect.Ether},
		{Provider: "ofi+sockets", DeviceName: "eth1_node2", NUMANode: 2, NetDevClass: netdetect.Ether},
		{Provider: "ofi+sockets", DeviceName: "eth2_node2", NUMANode: 2, NetDevClass: netdetect.Ether},
		{Provider: "ofi+sockets", DeviceName: "eth3_node2", NUMANode: 2, NetDevClass: netdetect.Ether}}

	aiCache := attachInfoCache{log: log, enabled: enabled}

	netCtx, cleanupFn := initCache(t, scanResults, &aiCache)
	defer cleanupFn(netCtx)

	for name, tc := range map[string]struct {
		numaNode          int
		numDevs           int
		serverNetDevClass uint32
	}{
		"info cache device with filtering for Ethernet with numa 0": {
			numaNode:          0,
			numDevs:           3,
			serverNetDevClass: netdetect.Ether,
		},
		"info cache device with filtering for Ethernet with numa 1": {
			numaNode:          1,
			numDevs:           1,
			serverNetDevClass: netdetect.Ether,
		},
		"info cache device with filtering for Ethernet with numa 2": {
			numaNode:          2,
			numDevs:           4,
			serverNetDevClass: netdetect.Ether,
		},
		"info cache device with filtering for Infiniband with numa 0": {
			numaNode:          0,
			numDevs:           1,
			serverNetDevClass: netdetect.Infiniband,
		},
		"info cache device with filtering for Infiniband with numa 1": {
			numaNode:          1,
			numDevs:           1,
			serverNetDevClass: netdetect.Infiniband,
		},
		"info cache device with filtering for Infiniband with numa 2": {
			numaNode:          2,
			numDevs:           0,
			serverNetDevClass: netdetect.Infiniband,
		},
	} {
		t.Run(name, func(t *testing.T) {
			resp := &mgmtpb.GetAttachInfoResp{NetDevClass: tc.serverNetDevClass}
			err := aiCache.initResponseCache(netCtx, resp, scanResults)
			common.AssertEqual(t, err, nil, "initResponseCache error")

			numDevs := len(aiCache.numaDeviceMarshResp[tc.numaNode])
			common.AssertEqual(t, numDevs, tc.numDevs,
				fmt.Sprintf("initResponseCache error - expected %d cached responses, got %d", tc.numDevs, numDevs))
		})
	}
}

// TestInfoCacheGetResponse reads an entry from the cache for the specified NUMA node
// and verifies that it got the exact response that was expected.
func TestInfoCacheGetResponse(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)
	enabled := atm.NewBool(true)
	scanResults := []*netdetect.FabricScan{
		{Provider: "ofi+sockets", DeviceName: "eth0", NUMANode: 0},
		{Provider: "ofi+sockets", DeviceName: "eth1", NUMANode: 1},
		{Provider: "ofi+sockets", DeviceName: "eth2", NUMANode: 2}}

	aiCache := attachInfoCache{log: log, enabled: enabled}

	netCtx, cleanupFn := initCache(t, scanResults, &aiCache)
	defer cleanupFn(netCtx)

	for name, tc := range map[string]struct {
		numaNode   int
		deviceName string
	}{
		"info cache response for numa 0": {
			numaNode:   0,
			deviceName: "eth0",
		},
		"info cache response for numa 1": {
			numaNode:   1,
			deviceName: "eth1",
		},
		"info cache response for numa 2": {
			numaNode:   2,
			deviceName: "eth2",
		},
		"info cache response for numa 3 with no devices": {
			numaNode:   3,
			deviceName: "eth0",
		},
	} {
		t.Run(name, func(t *testing.T) {
			res, err := aiCache.getResponse(tc.numaNode)
			common.AssertEqual(t, err, nil, "getResponse error")

			resp := &mgmtpb.GetAttachInfoResp{}
			if err = proto.Unmarshal(res, resp); err != nil {
				t.Errorf("Expected error on proto.Unmarshal, got %+v", err)
			}
			common.AssertTrue(t, resp.GetInterface() == tc.deviceName, fmt.Sprintf("Expected: %s, got %s", tc.deviceName, resp.GetInterface()))
		})
	}
}

// TestInfoCacheDefaultNumaNode reads an entry from the cache for the specified NUMA node
// and verifies the default response does not depend specifically on NUMA 0.
func TestInfoCacheDefaultNumaNode(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)
	enabled := atm.NewBool(true)
	scanResults := []*netdetect.FabricScan{
		{Provider: "ofi+sockets", DeviceName: "eth1", NUMANode: 1},
		{Provider: "ofi+sockets", DeviceName: "eth2", NUMANode: 2}}

	aiCache := attachInfoCache{log: log, enabled: enabled}

	netCtx, cleanupFn := initCache(t, scanResults, &aiCache)
	defer cleanupFn(netCtx)

	for name, tc := range map[string]struct {
		numaNode   int
		deviceName string
	}{
		"info cache response for numa 0 with no devices": {
			numaNode:   0,
			deviceName: "eth1",
		},
		"info cache response for numa 1": {
			numaNode:   1,
			deviceName: "eth1",
		},
		"info cache response for numa 2": {
			numaNode:   2,
			deviceName: "eth2",
		},
		"info cache response for numa 3 with no devices": {
			numaNode:   3,
			deviceName: "eth1",
		},
	} {
		t.Run(name, func(t *testing.T) {
			res, err := aiCache.getResponse(tc.numaNode)
			common.AssertEqual(t, err, nil, "getResponse error")

			resp := &mgmtpb.GetAttachInfoResp{}
			if err = proto.Unmarshal(res, resp); err != nil {
				t.Errorf("Expected error on proto.Unmarshal, got %+v", err)
			}
			common.AssertTrue(t, resp.GetInterface() == tc.deviceName, fmt.Sprintf("Expected: %s, got %s", tc.deviceName, resp.GetInterface()))
		})
	}
}

// TestInfoCacheLoadBalancer verifies that the load balancer provided the desired response.
// This test initializes the info cache with multiple devices per NUMA node and a predictable
// set of data for the cache.  The load balancer is expected to assign each cached response
// per NUMA node in linear order.
func TestInfoCacheLoadBalancer(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)
	enabled := atm.NewBool(true)
	scanResults := []*netdetect.FabricScan{
		{Provider: "ofi+sockets", DeviceName: "eth0_node0", NUMANode: 0},
		{Provider: "ofi+sockets", DeviceName: "eth1_node0", NUMANode: 0},
		{Provider: "ofi+sockets", DeviceName: "eth2_node0", NUMANode: 0},
		{Provider: "ofi+sockets", DeviceName: "eth3_node0", NUMANode: 0},
		{Provider: "ofi+sockets", DeviceName: "eth0_node1", NUMANode: 1},
		{Provider: "ofi+sockets", DeviceName: "eth1_node1", NUMANode: 1},
		{Provider: "ofi+sockets", DeviceName: "eth1_node2", NUMANode: 1},
		{Provider: "ofi+sockets", DeviceName: "eth0_node2", NUMANode: 2},
		{Provider: "ofi+sockets", DeviceName: "eth1_node2", NUMANode: 2},
		{Provider: "ofi+sockets", DeviceName: "eth0_node3", NUMANode: 3},
		{Provider: "ofi+sockets", DeviceName: "eth0_node7", NUMANode: 7},
		{Provider: "ofi+sockets", DeviceName: "eth1_node7", NUMANode: 7},
		{Provider: "ofi+sockets", DeviceName: "eth2_node7", NUMANode: 7},
		{Provider: "ofi+sockets", DeviceName: "eth3_node7", NUMANode: 7},
		{Provider: "ofi+sockets", DeviceName: "eth4_node7", NUMANode: 7},
	}

	aiCache := attachInfoCache{log: log, enabled: enabled}

	netCtx, cleanupFn := initCache(t, scanResults, &aiCache)
	defer cleanupFn(netCtx)

	var results map[int][]byte
	var response map[int]*mgmtpb.GetAttachInfoResp
	results = make(map[int][]byte)
	response = make(map[int]*mgmtpb.GetAttachInfoResp)

	for name, tc := range map[string]struct {
		numaNode   int
		numDevices int
		deviceName string
		neighbor   string
	}{
		"load balancer infocache entry numa 0 with 4 devices": {
			numaNode:   0,
			numDevices: 4,
			deviceName: "eth0_node0",
			neighbor:   "eth1_node0",
		},
		"load balancer infocache entry numa 1 with 3 devices": {
			numaNode:   1,
			numDevices: 3,
			deviceName: "eth0_node1",
			neighbor:   "eth1_node1",
		},
		"load balancer infocache entry numa 2 with 2 devices": {
			numaNode:   2,
			numDevices: 2,
			deviceName: "eth0_node2",
			neighbor:   "eth1_node2",
		},
		"load balancer infocache entry numa 3 with 1 device": {
			numaNode:   3,
			numDevices: 1,
			deviceName: "eth0_node3",
			neighbor:   "eth0_node3",
		},
		// This entry shows that numa nodes entries need not be contiguous
		"load balancer infocache entry numa 7 with 5 devices": {
			numaNode:   7,
			numDevices: 5,
			deviceName: "eth0_node7",
			neighbor:   "eth1_node7",
		},
	} {
		t.Run(name, func(t *testing.T) {
			for i := 0; i < tc.numDevices+2; i++ {
				var err error
				results[i], err = aiCache.getResponse(tc.numaNode)
				common.AssertEqual(t, err, nil, "getResponse error")
				response[i] = &mgmtpb.GetAttachInfoResp{}
				if err = proto.Unmarshal(results[i], response[i]); err != nil {
					t.Errorf("Expected error on proto.Unmarshal, got %+v", err)
				}
			}

			// verifies that the load balancer rolled back to the beginning of the list
			common.AssertTrue(t, response[0].GetInterface() == response[tc.numDevices].GetInterface(),
				fmt.Sprintf("expected: %s, got %s", response[0].GetInterface(), response[tc.numDevices].GetInterface()))

			// verifies that the device name is exactly the one expected
			common.AssertTrue(t, response[0].GetInterface() == tc.deviceName,
				fmt.Sprintf("expected: %s, got %s", tc.deviceName, response[0].GetInterface()))

			// verifies that the neighbor response is what was expected
			common.AssertTrue(t, response[tc.numDevices+1].GetInterface() == tc.neighbor,
				fmt.Sprintf("expected: %s, got %s", tc.neighbor, response[tc.numDevices+1].GetInterface()))
		})
	}
}

// getResponse is called as a concurrent routine to access the info cache.
// The cache contains responses with interface names specific to each NUMA node.
// Aside from actually generating a failure due to a race condition, the response
// data is checked to make sure it contains a device known to be associated with the
// given numa node.
func getResponse(t *testing.T, aiCache *attachInfoCache, numaNode int, wg *sync.WaitGroup) {
	defer wg.Done()
	res, err := aiCache.getResponse(numaNode)
	common.AssertEqual(t, err, nil, "TestInfoCacheConcurrentAccess getResponse error")

	resp := &mgmtpb.GetAttachInfoResp{}
	if err = proto.Unmarshal(res, resp); err != nil {
		t.Errorf("Expected error on proto.Unmarshal, got %+v", err)
	}

	deviceName := fmt.Sprintf("_node%d", numaNode)
	if !strings.HasSuffix(resp.GetInterface(), deviceName) {
		t.Errorf("TestInfoCacheConcurrentAccess response mismatch.  Devicename: %s, does not have suffix: %s", resp.GetInterface(), deviceName)
	}
}

// TestInfoCacheConcurrentAccess launches maxConcurrent go routines
// that concurrently access the info cache.  This test verifies that there are
// no race conditions accessing the shared cache.
func TestInfoCacheConcurrentAccess(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)
	enabled := atm.NewBool(true)
	scanResults := []*netdetect.FabricScan{
		{Provider: "ofi+sockets", DeviceName: "eth0_node0", NUMANode: 0, Priority: 0},
		{Provider: "ofi+sockets", DeviceName: "eth1_node0", NUMANode: 0, Priority: 1},
		{Provider: "ofi+sockets", DeviceName: "eth2_node0", NUMANode: 0, Priority: 2},
		{Provider: "ofi+sockets", DeviceName: "eth3_node0", NUMANode: 0, Priority: 3},
		{Provider: "ofi+sockets", DeviceName: "eth0_node1", NUMANode: 1, Priority: 0},
		{Provider: "ofi+sockets", DeviceName: "eth1_node1", NUMANode: 1, Priority: 1},
		{Provider: "ofi+sockets", DeviceName: "eth0_node2", NUMANode: 2, Priority: 0},
		{Provider: "ofi+sockets", DeviceName: "eth1_node2", NUMANode: 2, Priority: 1},
		{Provider: "ofi+sockets", DeviceName: "eth2_node2", NUMANode: 2, Priority: 2},
		{Provider: "ofi+sockets", DeviceName: "eth3_node2", NUMANode: 2, Priority: 3}}

	aiCache := attachInfoCache{log: log, enabled: enabled}

	netCtx, cleanupFn := initCache(t, scanResults, &aiCache)
	defer cleanupFn(netCtx)

	var wg sync.WaitGroup
	maxNumaNodes := 3
	rand.Seed(time.Now().UnixNano())
	for i := 0; i < maxConcurrent; i++ {
		wg.Add(1)
		go func(n int) {
			time.Sleep(time.Duration(rand.Intn(100)) * time.Microsecond)
			getResponse(t, &aiCache, n, &wg)
		}(i % maxNumaNodes)
	}
	wg.Wait()
}
