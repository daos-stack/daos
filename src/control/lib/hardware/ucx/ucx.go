//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ucx

import (
	"context"
	"strings"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/dlopen"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
)

// NewProvider creates a new UCX data provider.
func NewProvider(log logging.Logger) *Provider {
	return &Provider{
		log: log,
	}
}

// Provider provides information from UCX's API.
type Provider struct {
	log logging.Logger
}

// GetFabricInterfaces harvests the collection of fabric interfaces from UCX.
func (p *Provider) GetFabricInterfaces(ctx context.Context) (*hardware.FabricInterfaceSet, error) {
	uctHdl, err := openUCT()
	if err != nil {
		return nil, err
	}
	defer uctHdl.Close()

	components, cleanupComp, err := getUCTComponents(uctHdl)
	if err != nil {
		return nil, err
	}
	defer func() {
		if err := cleanupComp(); err != nil {
			p.log.Errorf("error cleaning up UCT components: %s", err.Error())
		}
	}()

	fis := hardware.NewFabricInterfaceSet()

	for _, comp := range components {
		mdResources, err := getMDResourceNames(uctHdl, comp)
		if err != nil {
			p.log.Error(err.Error())
			continue
		}

		cfg, cleanupCfg, err := getComponentMDConfig(uctHdl, comp)
		if err != nil {
			p.log.Error(err.Error())
			continue
		}
		defer func(name string) {
			if err := cleanupCfg(); err != nil {
				p.log.Error(err.Error())
			}
		}(comp.name)

		netDevs, err := p.getCompNetworkDevices(uctHdl, comp, mdResources, cfg)
		if err != nil {
			p.log.Error(err.Error())
			continue
		}

		if err := p.addFabricDevices(netDevs, fis); err != nil {
			p.log.Error(err.Error())
		}
	}

	return fis, nil
}

func (p *Provider) getCompNetworkDevices(uctHdl *dlopen.LibHandle, comp *uctComponent,
	mdResources []string, cfg *uctMDConfig) ([]*transportDev, error) {
	netDevs := make([]*transportDev, 0)
	for _, mdName := range mdResources {
		md, cleanupMD, err := openMDResource(uctHdl, comp, mdName, cfg)
		if err != nil {
			return nil, err
		}
		defer func() {
			if err := cleanupMD(); err != nil {
				p.log.Errorf(err.Error())
			}
		}()

		tlDevs, err := getMDTransportDevices(uctHdl, md)
		if err != nil {
			return nil, err
		}

		for _, dev := range tlDevs {
			if !dev.isNetwork() {
				continue
			}

			netDevs = append(netDevs, dev)
		}
	}

	return netDevs, nil
}

func (p *Provider) addFabricDevices(netDevs []*transportDev, fis *hardware.FabricInterfaceSet) error {
	allDevs := common.NewStringSet()
	for _, dev := range netDevs {
		allDevs.AddUnique(dev.device)
	}

	for _, dev := range netDevs {
		// the device name is in a format like "mlx5_0:1"
		osDev := strings.Split(dev.device, ":")[0]

		fis.Update(&hardware.FabricInterface{
			Name:      getExternalName(dev.device, allDevs.ToSlice()),
			OSName:    osDev,
			Providers: p.getProviderSet(dev.transport),
		})
	}

	return nil
}

// getExternalName constructs the name that must be used by DAOS to specify the fabric device. For
// UCX, this is a comma-separated list of all device names.
func getExternalName(dev string, allDevs []string) string {
	// To ensure unique names, each list has the main device string first.
	ordered := []string{dev}
	for _, d := range allDevs {
		if d != dev {
			ordered = append(ordered, d)
		}
	}

	return strings.Join(ordered, ",")
}

func (p *Provider) getProviderSet(transport string) common.StringSet {
	genericTransport := strings.Split(transport, "_")[0]

	providers := common.NewStringSet(transportToDAOSProvider(transport))
	if shouldAddGeneric(transport) {
		if err := providers.AddUnique(transportToDAOSProvider(genericTransport)); err != nil {
			p.log.Error(err.Error())
		}
	}
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

func transportToDAOSProvider(transport string) string {
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
