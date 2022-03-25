//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hwprov

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwloc"
	"github.com/daos-stack/daos/src/control/lib/hardware/libfabric"
	"github.com/daos-stack/daos/src/control/lib/hardware/sysfs"
	"github.com/daos-stack/daos/src/control/lib/hardware/ucx"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestHwprov_DefaultTopologyProvider(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	expResult := hardware.NewTopologyFactory(
		&hardware.WeightedTopologyProvider{
			Provider: hwloc.NewProvider(log),
			Weight:   100,
		},
		&hardware.WeightedTopologyProvider{
			Provider: sysfs.NewProvider(log),
			Weight:   90,
		},
	)

	result := DefaultTopologyProvider(log)

	if diff := cmp.Diff(expResult, result,
		cmp.AllowUnexported(hardware.TopologyFactory{}),
		cmpopts.IgnoreUnexported(hwloc.Provider{}),
		cmpopts.IgnoreUnexported(sysfs.Provider{}),
	); diff != "" {
		t.Fatalf("(-want, +got)\n%s\n", diff)
	}
}

func TestHwprov_DefaultProcessNUMAProvider(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	expResult := hwloc.NewProvider(log)

	result := DefaultProcessNUMAProvider(log)

	if diff := cmp.Diff(expResult, result,
		cmpopts.IgnoreUnexported(hwloc.Provider{}),
	); diff != "" {
		t.Fatalf("(-want, +got)\n%s\n", diff)
	}
}

func TestHwprov_DefaultFabricInterfaceProviders(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	expResult := []hardware.FabricInterfaceProvider{
		libfabric.NewProvider(log),
		sysfs.NewProvider(log),
		ucx.NewProvider(log),
	}

	result := DefaultFabricInterfaceProviders(log)

	if diff := cmp.Diff(expResult, result,
		cmpopts.IgnoreUnexported(libfabric.Provider{}),
		cmpopts.IgnoreUnexported(sysfs.Provider{}),
		cmpopts.IgnoreUnexported(ucx.Provider{}),
	); diff != "" {
		t.Fatalf("(-want, +got)\n%s\n", diff)
	}

}

func TestHwprov_DefaultNetDevClassProvider(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	expResult := sysfs.NewProvider(log)

	result := DefaultNetDevClassProvider(log)

	if diff := cmp.Diff(expResult, result,
		cmpopts.IgnoreUnexported(sysfs.Provider{}),
	); diff != "" {
		t.Fatalf("(-want, +got)\n%s\n", diff)
	}
}

func TestHwprov_DefaultFabricScannerConfig(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	expResult := &hardware.FabricScannerConfig{
		TopologyProvider:         DefaultTopologyProvider(log),
		FabricInterfaceProviders: DefaultFabricInterfaceProviders(log),
		NetDevClassProvider:      DefaultNetDevClassProvider(log),
	}

	result := DefaultFabricScannerConfig(log)

	if diff := cmp.Diff(expResult, result,
		cmpopts.IgnoreUnexported(
			hardware.TopologyFactory{},
			hwloc.Provider{},
			libfabric.Provider{},
			sysfs.Provider{},
		),
		common.CmpOptIgnoreFieldAnyType("log"),
	); diff != "" {
		t.Fatalf("(-want, +got)\n%s\n", diff)
	}
}

func TestHwprov_DefaultFabricScanner(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	expResult, err := hardware.NewFabricScanner(log, &hardware.FabricScannerConfig{
		TopologyProvider:         DefaultTopologyProvider(log),
		FabricInterfaceProviders: DefaultFabricInterfaceProviders(log),
		NetDevClassProvider:      DefaultNetDevClassProvider(log),
	})
	if err != nil {
		t.Fatal(err)
	}

	result := DefaultFabricScanner(log)

	if diff := cmp.Diff(expResult, result,
		cmp.AllowUnexported(
			hardware.FabricScanner{},
			hardware.TopologyFactory{},
		),
		cmpopts.IgnoreUnexported(
			hwloc.Provider{},
			libfabric.Provider{},
			sysfs.Provider{},
		),
		common.CmpOptIgnoreFieldAnyType("log"),
		cmpopts.IgnoreFields(hardware.FabricScanner{}, "mutex"),
	); diff != "" {
		t.Fatalf("(-want, +got)\n%s\n", diff)
	}
}
