//
// (C) Copyright 2021-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// Package network provides constructors for cross-package composable hardware providers
// related to networking.
package network

// NB: Carefully consider whether adding a new package dependency is necessary, as it
// forces all users of this package to import it regardless of whether or not they
// use the new dependency.

import (
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/cart"
	"github.com/daos-stack/daos/src/control/lib/hardware/defaults/topology"
	"github.com/daos-stack/daos/src/control/lib/hardware/sysfs"
	"github.com/daos-stack/daos/src/control/logging"
)

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
		TopologyProvider:         topology.DefaultProvider(log),
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
