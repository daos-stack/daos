//
// (C) Copyright 2021 Intel Corporation.
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
	"github.com/daos-stack/daos/src/control/logging"
)

func TestHwprov_DefaultTopologyProvider(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	expResult := hwloc.NewProvider(log)

	result := DefaultTopologyProvider(log)

	if diff := cmp.Diff(expResult, result,
		cmpopts.IgnoreUnexported(hwloc.Provider{}),
	); diff != "" {
		t.Fatalf("(-want, +got)\n%s\n", diff)
	}
}

func TestHwprov_DefaultFabricInterfaceProvider(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	expResult := []hardware.FabricInterfaceProvider{
		libfabric.NewProvider(log),
	}

	result := DefaultFabricInterfaceProviders(log)

	if diff := cmp.Diff(expResult, result,
		cmpopts.IgnoreUnexported(libfabric.Provider{}),
	); diff != "" {
		t.Fatalf("(-want, +got)\n%s\n", diff)
	}

}

func TestHwprov_DefaultNetDevClassProvider(t *testing.T) {
	expResult := sysfs.NewProvider()

	result := DefaultNetDevClassProvider()

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
		NetDevClassProvider:      DefaultNetDevClassProvider(),
	}

	result := DefaultFabricScannerConfig(log)

	if diff := cmp.Diff(expResult, result,
		cmpopts.IgnoreUnexported(
			hwloc.Provider{},
			libfabric.Provider{},
			sysfs.Provider{},
		),
		common.CmpOptIgnoreField("log"),
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
		NetDevClassProvider:      DefaultNetDevClassProvider(),
	})
	if err != nil {
		t.Fatal(err)
	}

	result := DefaultFabricScanner(log)

	if diff := cmp.Diff(expResult, result,
		cmp.AllowUnexported(
			hardware.FabricScanner{},
		),
		cmpopts.IgnoreUnexported(
			hwloc.Provider{},
			libfabric.Provider{},
			sysfs.Provider{},
		),
		common.CmpOptIgnoreField("log"),
	); diff != "" {
		t.Fatalf("(-want, +got)\n%s\n", diff)
	}
}
