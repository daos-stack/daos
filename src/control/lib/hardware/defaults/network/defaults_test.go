//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package network_test

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/cart"
	"github.com/daos-stack/daos/src/control/lib/hardware/defaults/network"
	"github.com/daos-stack/daos/src/control/lib/hardware/defaults/topology"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwloc"
	"github.com/daos-stack/daos/src/control/lib/hardware/sysfs"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestFabric_DefaultFabricInterfaceProviders(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	expResult := []hardware.FabricInterfaceProvider{
		cart.NewProvider(log),
		sysfs.NewProvider(log),
	}

	result := network.DefaultFabricInterfaceProviders(log)

	if diff := cmp.Diff(expResult, result,
		cmpopts.IgnoreUnexported(cart.Provider{}),
		cmpopts.IgnoreUnexported(sysfs.Provider{}),
	); diff != "" {
		t.Fatalf("(-want, +got)\n%s\n", diff)
	}

}

func TestFabric_DefaultNetDevClassProvider(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	expResult := sysfs.NewProvider(log)

	result := network.DefaultNetDevClassProvider(log)

	if diff := cmp.Diff(expResult, result,
		cmpopts.IgnoreUnexported(sysfs.Provider{}),
	); diff != "" {
		t.Fatalf("(-want, +got)\n%s\n", diff)
	}
}

func TestFabric_DefaultFabricScannerConfig(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	expResult := &hardware.FabricScannerConfig{
		TopologyProvider:         topology.DefaultProvider(log),
		FabricInterfaceProviders: network.DefaultFabricInterfaceProviders(log),
		NetDevClassProvider:      network.DefaultNetDevClassProvider(log),
	}

	result := network.DefaultFabricScannerConfig(log)

	if diff := cmp.Diff(expResult, result,
		cmpopts.IgnoreUnexported(
			hardware.TopologyFactory{},
			hwloc.Provider{},
			cart.Provider{},
			sysfs.Provider{},
		),
		test.CmpOptIgnoreFieldAnyType("log"),
	); diff != "" {
		t.Fatalf("(-want, +got)\n%s\n", diff)
	}
}

func TestFabric_DefaultFabricScanner(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	expResult, err := hardware.NewFabricScanner(log, &hardware.FabricScannerConfig{
		TopologyProvider:         topology.DefaultProvider(log),
		FabricInterfaceProviders: network.DefaultFabricInterfaceProviders(log),
		NetDevClassProvider:      network.DefaultNetDevClassProvider(log),
	})
	if err != nil {
		t.Fatal(err)
	}

	result := network.DefaultFabricScanner(log)

	if diff := cmp.Diff(expResult, result,
		cmp.AllowUnexported(
			hardware.FabricScanner{},
			hardware.TopologyFactory{},
		),
		cmpopts.IgnoreUnexported(
			hwloc.Provider{},
			cart.Provider{},
			sysfs.Provider{},
		),
		test.CmpOptIgnoreFieldAnyType("log"),
		cmpopts.IgnoreFields(hardware.FabricScanner{}, "mutex"),
	); diff != "" {
		t.Fatalf("(-want, +got)\n%s\n", diff)
	}
}

func TestFabric_DefaultNetDevStateProvider(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	result := network.DefaultNetDevStateProvider(log)

	if diff := cmp.Diff(sysfs.NewProvider(log), result,
		cmpopts.IgnoreUnexported(sysfs.Provider{}),
	); diff != "" {
		t.Fatalf("(-want, +got)\n%s\n", diff)
	}
}
