//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hwprov

import (
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwloc"
	"github.com/daos-stack/daos/src/control/lib/hardware/libfabric"
	"github.com/daos-stack/daos/src/control/lib/hardware/sysfs"
	"github.com/daos-stack/daos/src/control/lib/hardware/ucx"
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
		libfabric.NewProvider(log),
		sysfs.NewProvider(log),
		ucx.NewProvider(log),
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

// Init loads up any dynamic libraries that need to be loaded at runtime.
func Init(log logging.Logger) (func(), error) {
	initFns := []func() (func(), error){
		libfabric.Load,
		ucx.Load,
	}

	cleanupFns := make([]func(), 0)
	numLoaded := 0

	for _, loadLib := range initFns {
		if cleanupLib, err := loadLib(); err == nil {
			numLoaded++
			cleanupFns = append(cleanupFns, cleanupLib)
		} else {
			log.Debugf("failed to load library: %s", err)
			if !hardware.IsUnsupportedFabric(err) {
				log.Error(err.Error())
			}
		}
	}

	if numLoaded == 0 {
		return nil, errors.New("unable to load any supported fabric libraries")
	}

	return func() {
		// Unload libraries in reverse order
		for i := len(cleanupFns) - 1; i >= 0; i-- {
			cleanupFns[i]()
		}
	}, nil
}
