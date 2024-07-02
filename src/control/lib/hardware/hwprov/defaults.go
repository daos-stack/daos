//
// (C) Copyright 2021-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hwprov

import (
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/cart"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwloc"
	"github.com/daos-stack/daos/src/control/lib/hardware/pciutils"
	"github.com/daos-stack/daos/src/control/lib/hardware/sysfs"
	"github.com/daos-stack/daos/src/control/logging"
)

// DefaultTopologyProvider gets the default hardware topology provider.
func DefaultTopologyProvider(log logging.Logger) hardware.TopologyProvider {
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

// DefaultFabricInterfaceProviders returns the default fabric interface providers.
func DefaultFabricInterfaceProviders(log logging.Logger) []hardware.FabricInterfaceProvider {
	return []hardware.FabricInterfaceProvider{
		cart.NewProvider(log),
		sysfs.NewProvider(log),
	}
}

// DefaultNetDevClassProvider gets the default provider for the network device class.
func DefaultNetDevClassProvider(log logging.Logger) hardware.NetDevClassProvider {
	return sysfs.NewProvider(log)
}

// DefaultFabricScannerConfig gets a default FabricScanner configuration.
func DefaultFabricScannerConfig(log logging.Logger) *hardware.FabricScannerConfig {
	return &hardware.FabricScannerConfig{
		TopologyProvider:         DefaultTopologyProvider(log),
		FabricInterfaceProviders: DefaultFabricInterfaceProviders(log),
		NetDevClassProvider:      DefaultNetDevClassProvider(log),
	}
}

// DefaultFabricScanner gets a FabricScanner using the default configuration.
func DefaultFabricScanner(log logging.Logger) *hardware.FabricScanner {
	fs, err := hardware.NewFabricScanner(log, DefaultFabricScannerConfig(log))
	if err != nil {
		panic(err)
	}

	return fs
}

// DefaultNetDevStateProvider gets the default provider for getting the fabric interface state.
func DefaultNetDevStateProvider(log logging.Logger) hardware.NetDevStateProvider {
	return sysfs.NewProvider(log)
}

// DefaultIOMMUDetector gets the default provider for the IOMMU detector.
func DefaultIOMMUDetector(log logging.Logger) hardware.IOMMUDetector {
	return sysfs.NewProvider(log)
}

// DefaultPCIeLinkStatsProvider gets the default provider for retrieving PCIe link stats.
func DefaultPCIeLinkStatsProvider() hardware.PCIeLinkStatsProvider {
	return pciutils.NewPCIeLinkStatsProvider()
}
