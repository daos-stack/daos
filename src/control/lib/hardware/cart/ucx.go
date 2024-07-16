//
// (C) Copyright 2022-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package cart

import (
	"strings"

	"github.com/daos-stack/daos/src/control/lib/hardware"
)

const (
	ucxTCPPriority      = 25
	ucxCatchallPriority = 99
)

// getOSNameFromUCXDevice gets the OS-level device name based on the UCX device name.
func getOSNameFromUCXDevice(ucxDevice string) string {
	// the device name is in a format like "mlx5_0:1"
	return strings.Split(ucxDevice, ":")[0]
}

// getProviderSetFromUCXTransport gets the set of DAOS providers associated with a UCX transport.
func getProviderSetFromUCXTransport(transport string) *hardware.FabricProviderSet {
	if transport == "" {
		return hardware.NewFabricProviderSet()
	}
	genericTransport := strings.Split(transport, "_")[0]

	providers := hardware.NewFabricProviderSet()
	priority := 0 // by default use the highest
	daosProv := ucxTransportToDAOSProviders(transport)
	for _, p := range daosProv {
		if p == "ucx+tcp" {
			priority = ucxTCPPriority // TCP is less desirable than other options if this is Infiniband
		}
		providers.Add(
			&hardware.FabricProvider{
				Name:     p,
				Priority: priority,
			},
		)
	}
	if shouldAddGeneric(transport) {
		providers.Add(&hardware.FabricProvider{
			Name:     "ucx+" + genericTransport,
			Priority: priority,
		})
	}
	// Any interface with at least one provider should allow ucx+all
	providers.Add(&hardware.FabricProvider{
		Name:     "ucx+all",
		Priority: ucxCatchallPriority,
	})
	return providers
}

func shouldAddGeneric(transport string) bool {
	genericTransportAliases := []string{"rc", "ud", "dc"}
	for _, alias := range genericTransportAliases {
		if strings.HasPrefix(transport, alias+"_") {
			return true
		}
	}
	return false
}

// ucxTransportToDAOSProviders translates the UCX transport type to a set of DAOS fabric provider
// strings.
func ucxTransportToDAOSProviders(transport string) []string {
	prefix := "ucx+"
	provs := []string{prefix + transport}
	alias := ucxTransportToAlias(transport)
	if alias != transport {
		provs = append(provs, prefix+alias)
	}
	return provs
}

func ucxTransportToAlias(transport string) string {
	transportPieces := strings.Split(transport, "_")
	if len(transportPieces) < 2 {
		return transport
	}

	// Transport strings from the library need to be translated to the supported aliases.
	// UCX transport aliases:
	// https://openucx.readthedocs.io/en/master/faq.html#list-of-main-transports-and-aliases
	switch {
	case transportPieces[1] == "verbs":
		transportPieces[1] = "v"
	case strings.HasPrefix(transportPieces[1], "mlx"):
		// accelerated Mellanox transport
		transportPieces[1] = "x"
	}
	return strings.Join(transportPieces, "_")
}
