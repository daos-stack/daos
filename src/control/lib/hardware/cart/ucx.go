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

	priority := 0 // by default use the highest
	daosProv := ucxTransportToDAOSProvider(transport)
	if daosProv == "ucx+tcp" {
		priority = ucxTCPPriority // TCP is less desirable than other options if this is Infiniband
	}
	providers := hardware.NewFabricProviderSet(
		&hardware.FabricProvider{
			Name:     daosProv,
			Priority: priority,
		},
	)
	if shouldAddGeneric(transport) {
		providers.Add(&hardware.FabricProvider{
			Name:     ucxTransportToDAOSProvider(genericTransport),
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

// ucxTransportToDAOSProvider translates the UCX transport type to a DAOS fabric provider string.
func ucxTransportToDAOSProvider(transport string) string {
	prefix := "ucx+"
	transportPieces := strings.Split(transport, "_")
	if len(transportPieces) < 2 {
		return prefix + transport
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
	return prefix + strings.Join(transportPieces, "_")
}
