//
// (C) Copyright 2021-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package topology_test

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/defaults/topology"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwloc"
	"github.com/daos-stack/daos/src/control/lib/hardware/sysfs"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestTopology_DefaultProvider(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

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

	result := topology.DefaultProvider(log)

	if diff := cmp.Diff(expResult, result,
		cmp.AllowUnexported(hardware.TopologyFactory{}),
		cmpopts.IgnoreUnexported(hwloc.Provider{}),
		cmpopts.IgnoreUnexported(sysfs.Provider{}),
	); diff != "" {
		t.Fatalf("(-want, +got)\n%s\n", diff)
	}
}

func TestTopology_DefaultProcessNUMAProvider(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	expResult := hwloc.NewProvider(log)

	result := topology.DefaultProcessNUMAProvider(log)

	if diff := cmp.Diff(expResult, result,
		cmpopts.IgnoreUnexported(hwloc.Provider{}),
	); diff != "" {
		t.Fatalf("(-want, +got)\n%s\n", diff)
	}
}
