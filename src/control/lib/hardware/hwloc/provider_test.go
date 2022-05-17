//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hwloc

import (
	"context"
	"os"
	"path/filepath"
	"runtime"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestHwloc_CacheContext(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	ctx, err := CacheContext(context.Background(), log)
	if err != nil {
		t.Fatal(err)
	}

	topo, ok := ctx.Value(topoKey).(*topology)
	if !ok {
		t.Fatalf("key %q not added to context", topoKey)
	}
	if topo == nil {
		t.Fatal("topology was nil")
	}

	cleanup, ok := ctx.Value(cleanupKey).(func())
	if !ok {
		t.Fatalf("key %q not added to context", cleanupKey)
	}
	if cleanup == nil {
		t.Fatal("cleanup function was nil")
	}
	cleanup()
}

func TestHwloc_Cleanup(t *testing.T) {
	cleanupCalled := 0
	mockCleanup := func() {
		cleanupCalled++
	}

	for name, tc := range map[string]struct {
		ctx              context.Context
		expCleanupCalled int
	}{
		"no cleanup cached": {
			ctx: context.Background(),
		},
		"success": {
			ctx:              context.WithValue(context.Background(), cleanupKey, mockCleanup),
			expCleanupCalled: 1,
		},
	} {
		t.Run(name, func(t *testing.T) {
			cleanupCalled = 0

			Cleanup(tc.ctx)
			test.AssertEqual(t, tc.expCleanupCalled, cleanupCalled, "")
		})
	}
}

func TestHwlocProvider_GetTopology_Samples(t *testing.T) {
	_, filename, _, _ := runtime.Caller(0)
	testdataDir := filepath.Join(filepath.Dir(filename), "testdata")

	// sample hwloc topologies taken from real systems
	for name, tc := range map[string]struct {
		hwlocXMLFile string
		expResult    *hardware.Topology
	}{
		"boro-84": {
			hwlocXMLFile: filepath.Join(testdataDir, "boro-84.xml"),
			expResult: &hardware.Topology{
				NUMANodes: map[uint]*hardware.NUMANode{
					0: hardware.MockNUMANode(0, 24).
						WithPCIBuses(
							[]*hardware.PCIBus{
								hardware.NewPCIBus(0, 0, 2),
								hardware.NewPCIBus(0, 0x17, 0x18),
								hardware.NewPCIBus(0, 0x3a, 0x3e),
							},
						).
						WithDevices(
							[]*hardware.PCIDevice{
								{
									Name:    "ib0",
									Type:    hardware.DeviceTypeNetInterface,
									PCIAddr: *hardware.MustNewPCIAddress("0000:18:00.0"),
								},
								{
									Name:    "hfi1_0",
									Type:    hardware.DeviceTypeOFIDomain,
									PCIAddr: *hardware.MustNewPCIAddress("0000:18:00.0"),
								},
								{
									Name:    "eth0",
									Type:    hardware.DeviceTypeNetInterface,
									PCIAddr: *hardware.MustNewPCIAddress("0000:3d:00.0"),
								},
								{
									Name:    "i40iw1",
									Type:    hardware.DeviceTypeOFIDomain,
									PCIAddr: *hardware.MustNewPCIAddress("0000:3d:00.0"),
								},
								{
									Name:    "eth1",
									Type:    hardware.DeviceTypeNetInterface,
									PCIAddr: *hardware.MustNewPCIAddress("0000:3d:00.1"),
								},
								{
									Name:    "i40iw0",
									Type:    hardware.DeviceTypeOFIDomain,
									PCIAddr: *hardware.MustNewPCIAddress("0000:3d:00.1"),
								},
								{
									Name:    "sda",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:00:11.5"),
								},
							},
						),
					1: hardware.MockNUMANode(1, 24, 24),
				},
			},
		},
		"wolf-133": {
			hwlocXMLFile: filepath.Join(testdataDir, "wolf-133.xml"),
			expResult: &hardware.Topology{
				NUMANodes: map[uint]*hardware.NUMANode{
					0: hardware.MockNUMANode(0, 24).
						WithPCIBuses(
							[]*hardware.PCIBus{
								hardware.NewPCIBus(0, 0, 2),
								hardware.NewPCIBus(0, 0x17, 0x18),
								hardware.NewPCIBus(0, 0x3a, 0x3e),
								hardware.NewPCIBus(0, 0x5d, 0x5f),
							},
						).
						WithDevices(
							[]*hardware.PCIDevice{
								{
									Name:    "ib0",
									Type:    hardware.DeviceTypeNetInterface,
									PCIAddr: *hardware.MustNewPCIAddress("0000:18:00.0"),
								},
								{
									Name:    "hfi1_0",
									Type:    hardware.DeviceTypeOFIDomain,
									PCIAddr: *hardware.MustNewPCIAddress("0000:18:00.0"),
								},
								{
									Name:    "eth0",
									Type:    hardware.DeviceTypeNetInterface,
									PCIAddr: *hardware.MustNewPCIAddress("0000:3d:00.0"),
								},
								{
									Name:    "i40iw1",
									Type:    hardware.DeviceTypeOFIDomain,
									PCIAddr: *hardware.MustNewPCIAddress("0000:3d:00.0"),
								},
								{
									Name:    "eth1",
									Type:    hardware.DeviceTypeNetInterface,
									PCIAddr: *hardware.MustNewPCIAddress("0000:3d:00.1"),
								},
								{
									Name:    "i40iw0",
									Type:    hardware.DeviceTypeOFIDomain,
									PCIAddr: *hardware.MustNewPCIAddress("0000:3d:00.1"),
								},
								{
									Name:    "sda",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:00:11.5"),
								},
							},
						),
					1: hardware.MockNUMANode(1, 24, 24).
						WithPCIBuses(
							[]*hardware.PCIBus{
								hardware.NewPCIBus(0, 0xae, 0xaf),
							},
						).
						WithDevices(
							[]*hardware.PCIDevice{
								{
									Name:    "ib1",
									Type:    hardware.DeviceTypeNetInterface,
									PCIAddr: *hardware.MustNewPCIAddress("0000:af:00.0"),
								},
								{
									Name:    "hfi1_1",
									Type:    hardware.DeviceTypeOFIDomain,
									PCIAddr: *hardware.MustNewPCIAddress("0000:af:00.0"),
								},
							},
						),
				},
			},
		},
		"no devices": {
			hwlocXMLFile: filepath.Join(testdataDir, "gcp_topology.xml"),
			expResult: &hardware.Topology{
				NUMANodes: map[uint]*hardware.NUMANode{
					0: hardware.MockNUMANode(0, 8).
						WithPCIBuses(
							[]*hardware.PCIBus{
								hardware.NewPCIBus(0, 0, 0),
							},
						),
					1: hardware.MockNUMANode(1, 8, 8),
				},
			},
		},
		"multiport": {
			hwlocXMLFile: filepath.Join(testdataDir, "multiport_hfi_topology.xml"),
			expResult: &hardware.Topology{
				NUMANodes: map[uint]*hardware.NUMANode{
					0: hardware.MockNUMANode(0, 8).
						WithPCIBuses(
							[]*hardware.PCIBus{
								hardware.NewPCIBus(0, 0, 7),
							},
						).
						WithDevices(
							[]*hardware.PCIDevice{
								{
									Name:    "ib0",
									Type:    hardware.DeviceTypeNetInterface,
									PCIAddr: *hardware.MustNewPCIAddress("0000:02:00.0"),
								},
								{
									Name:    "enp2s0",
									Type:    hardware.DeviceTypeNetInterface,
									PCIAddr: *hardware.MustNewPCIAddress("0000:02:00.0"),
								},
								{
									Name:    "mlx4_0",
									Type:    hardware.DeviceTypeOFIDomain,
									PCIAddr: *hardware.MustNewPCIAddress("0000:02:00.0"),
								},
								{
									Name:    "enp6s0",
									Type:    hardware.DeviceTypeNetInterface,
									PCIAddr: *hardware.MustNewPCIAddress("0000:06:00.0"),
								},
								{
									Name:    "sda",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:00:1f.2"),
								},
								{
									Name:    "sdb",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:00:1f.2"),
								},
							},
						),
					1: hardware.MockNUMANode(1, 8, 8).
						WithPCIBuses(
							[]*hardware.PCIBus{
								hardware.NewPCIBus(0, 0x80, 0x84),
							},
						).
						WithDevices(
							[]*hardware.PCIDevice{
								{
									Name:    "ib1",
									Type:    hardware.DeviceTypeNetInterface,
									PCIAddr: *hardware.MustNewPCIAddress("0000:83:00.0"),
								},
								{
									Name:    "ib2",
									Type:    hardware.DeviceTypeNetInterface,
									PCIAddr: *hardware.MustNewPCIAddress("0000:83:00.0"),
								},
								{
									Name:    "mlx4_1",
									Type:    hardware.DeviceTypeOFIDomain,
									PCIAddr: *hardware.MustNewPCIAddress("0000:83:00.0"),
								},
							},
						),
				},
			},
		},
		"no NUMA nodes in topology": {
			hwlocXMLFile: filepath.Join(testdataDir, "no-numa-nodes.xml"),
			expResult: &hardware.Topology{
				NUMANodes: map[uint]*hardware.NUMANode{
					0: func() *hardware.NUMANode {
						node := hardware.MockNUMANode(0, 4).
							WithPCIBuses(
								[]*hardware.PCIBus{
									hardware.NewPCIBus(0, 0, 3),
									hardware.NewPCIBus(0, 0x17, 0x18),
								},
							).
							WithDevices(
								[]*hardware.PCIDevice{
									{
										Name:    "ib0",
										Type:    hardware.DeviceTypeNetInterface,
										PCIAddr: *hardware.MustNewPCIAddress("0000:18:00.0"),
									},
									{
										Name:    "hfi1_0",
										Type:    hardware.DeviceTypeOFIDomain,
										PCIAddr: *hardware.MustNewPCIAddress("0000:18:00.0"),
									},
									{
										Name:    "sda",
										Type:    hardware.DeviceTypeBlock,
										PCIAddr: *hardware.MustNewPCIAddress("0000:00:1f.2"),
									},
									{
										Name:    "sr0",
										Type:    hardware.DeviceTypeBlock,
										PCIAddr: *hardware.MustNewPCIAddress("0000:00:1f.2"),
									},
								},
							)
						return node
					}(),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			_, err := os.Stat(tc.hwlocXMLFile)
			test.AssertEqual(t, err, nil, "unable to read hwloc XML file")
			os.Setenv("HWLOC_XMLFILE", tc.hwlocXMLFile)
			defer os.Unsetenv("HWLOC_XMLFILE")

			provider := NewProvider(log)
			ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
			defer cancel()

			result, err := provider.GetTopology(ctx)
			if err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Errorf("(-want, +got)\n%s\n", diff)
			}
		})

	}
}

func TestHwloc_Provider_GetNUMANodeForPID_Parallel(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	runTest_GetNUMANodeForPID_Parallel(t, context.Background(), log)
}

func runTest_GetNUMANodeForPID_Parallel(t *testing.T, parent context.Context, log logging.Logger) {
	t.Helper()

	ctx, cancel := context.WithTimeout(parent, 10*time.Second)
	defer cancel()

	p := NewProvider(log)

	doneCh := make(chan error)
	total := 500

	// If we aren't testing with a cached context, reduce the number
	// of goroutines in order to avoid timing out on more complex
	// topologies.
	if _, err := topologyFromContext(ctx); err != nil {
		total = 128
	}

	for i := 0; i < total; i++ {
		go func() {
			_, err := p.GetNUMANodeIDForPID(ctx, int32(os.Getpid()))
			doneCh <- err
		}()
	}

	for i := 0; i < total; i++ {
		err := <-doneCh
		if err != nil && err != hardware.ErrNoNUMANodes {
			t.Fatal(err)
		}
	}
}

func TestHwloc_Provider_GetNUMANodeForPID_Parallel_CachedCtx(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	cachedCtx, err := CacheContext(context.Background(), log)
	if err != nil {
		t.Fatal(err)
	}
	defer Cleanup(cachedCtx)

	runTest_GetNUMANodeForPID_Parallel(t, cachedCtx, log)
}

func TestHwloc_Provider_findNUMANodeWithCPUSet(t *testing.T) {
	_, filename, _, _ := runtime.Caller(0)
	hwlocXMLFile := filepath.Join(filepath.Dir(filename), "testdata", "boro-84.xml")

	_, err := os.Stat(hwlocXMLFile)
	test.AssertEqual(t, err, nil, "unable to read hwloc XML file")
	os.Setenv("HWLOC_XMLFILE", hwlocXMLFile)
	defer os.Unsetenv("HWLOC_XMLFILE")

	provider := NewProvider(nil)

	ctx, cancelCtx := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancelCtx()
	testTopo, cleanup, err := provider.getRawTopology(ctx)
	if err != nil {
		t.Fatal(err)
	}
	defer cleanup()

	// Harvested from the NUMA nodes in our xml file
	numaCPUSet := map[uint]string{
		0: "0x000000ff,0xffff0000,0x00ffffff",
		1: "0xffffff00,0x0000ffff,0xff000000",
	}

	for name, tc := range map[string]struct {
		getCPUIn  func() (*cpuSet, func())
		expResult uint
		expErr    error
	}{
		"first NUMA": {
			getCPUIn: func() (*cpuSet, func()) {
				return mustCPUSetFromString(numaCPUSet[0], testTopo)
			},
			expResult: 0,
		},
		"second NUMA": {
			getCPUIn: func() (*cpuSet, func()) {
				return mustCPUSetFromString(numaCPUSet[1], testTopo)
			},
			expResult: 1,
		},
		"no match": {
			getCPUIn: func() (*cpuSet, func()) {
				return mustCPUSetFromString("0x00000000", testTopo)
			},
			expErr: errors.New("no NUMA node"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			provider.log = log

			cpuIn, cleanup := tc.getCPUIn()
			defer cleanup()

			result, err := provider.findNUMANodeWithCPUSet(testTopo, cpuIn)

			test.CmpErr(t, tc.expErr, err)
			test.AssertEqual(t, tc.expResult, result, "")
		})

	}
}
