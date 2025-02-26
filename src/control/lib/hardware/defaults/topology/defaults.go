//
// (C) Copyright 2021-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// Package topology provides constructors for cross-package composable hardware providers
// related to topology discovery and interrogation.
package topology

// NB: Carefully consider whether adding a new package dependency is necessary, as it
// forces all users of this package to import it regardless of whether or not they
// use the new dependency.

import (
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwloc"
	"github.com/daos-stack/daos/src/control/lib/hardware/sysfs"
	"github.com/daos-stack/daos/src/control/logging"
)

// DefaultProvider gets the default hardware topology provider.
func DefaultProvider(log logging.Logger) hardware.TopologyProvider {
	return hardware.NewTopologyFactory(
		&hardware.WeightedTopologyProvider{
			Provider: hwloc.NewProvider(log),
			Weight:   100,
		},
		&hardware.WeightedTopologyProvider{
			Provider: sysfs.NewProvider(log),
			Weight:   90,
		},
	)
}

// DefaultProcessNUMAProvider gets the default provider for process-related NUMA info.
func DefaultProcessNUMAProvider(log logging.Logger) hardware.ProcessNUMAProvider {
	return hwloc.NewProvider(log)
}

// DefaultIOMMUDetector gets the default provider for the IOMMU detector.
func DefaultIOMMUDetector(log logging.Logger) hardware.IOMMUDetector {
	return sysfs.NewProvider(log)
}
