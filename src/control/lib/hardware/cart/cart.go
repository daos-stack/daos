//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package cart

import (
	"context"
	"fmt"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
)

const (
	classLibFabric = "ofi"
	classUCX       = "ucx"
	classNA        = "na"
)

// crtFabricDevice is a single fabric device discovered by CART.
type crtFabricDevice struct {
	Class    string `json:"class"`
	Protocol string `json:"protocol"`
	Device   string `json:"device"`
}

// isUCX indicates whether this is a UCX device.
func (cfd *crtFabricDevice) IsUCX() bool {
	return cfd.Class == classUCX
}

// OSName returns the OS level network device name for this device.
func (cfd *crtFabricDevice) OSName() string {
	if cfd.IsUCX() {
		return getOSNameFromUCXDevice(cfd.Device)
	}
	return cfd.Device
}

// ProviderName returns the DAOS fabric provider name for this device's protocol.
func (cfd *crtFabricDevice) ProviderName() string {
	return fmt.Sprintf("%s+%s", cfd.Class, cfd.Protocol)
}

type getProtocolFn func(log logging.Logger, provider string) ([]*crtFabricDevice, error)

// Provider provides access to the CART API.
type Provider struct {
	log             logging.Logger
	getProtocolInfo getProtocolFn
}

// NewProvider creates a new CART Provider.
func NewProvider(log logging.Logger) *Provider {
	return &Provider{
		log: log,
	}
}

// GetFabricInterfaces fetches information about the system fabric interfaces via CART.
func (p *Provider) GetFabricInterfaces(ctx context.Context, provider string) (*hardware.FabricInterfaceSet, error) {
	if p == nil {
		return nil, errors.New("nil CART Provider")
	}

	ch := make(chan *fabricResult)
	go p.getFabricInterfaces(provider, ch)
	select {
	case <-ctx.Done():
		return nil, ctx.Err()
	case result := <-ch:
		return result.fiSet, result.err
	}
}

type fabricResult struct {
	fiSet *hardware.FabricInterfaceSet
	err   error
}

type providerPriorities map[string]int

func (p providerPriorities) getPriority(provName string) int {
	prio, ok := p[provName]
	if !ok {
		prio = len(p)
		p[provName] = prio
	}
	return prio
}

func (p *Provider) getFabricInterfaces(provider string, ch chan *fabricResult) {
	if p.getProtocolInfo == nil {
		p.getProtocolInfo = getProtocolInfo
	}

	devices, err := p.getProtocolInfo(p.log, provider)
	if err != nil {
		provMsg := ""
		if provider != "" {
			provMsg = fmt.Sprintf(" for provider %q", provider)
		}
		ch <- &fabricResult{
			err: errors.Wrapf(err, "fetching fabric interfaces%s", provMsg),
		}
		return
	}

	fis := hardware.NewFabricInterfaceSet()
	priorities := make(providerPriorities)
	for _, dev := range devices {
		fis.Update(crtFabricDeviceToFabricInterface(dev, priorities))
	}

	ch <- &fabricResult{
		fiSet: fis,
	}
}

func crtFabricDeviceToFabricInterface(dev *crtFabricDevice, priorities providerPriorities) *hardware.FabricInterface {
	return &hardware.FabricInterface{
		Name:      dev.Device,
		OSName:    dev.OSName(),
		Providers: getProviderSet(dev, priorities),
	}
}

// getProviderSet returns a set of one or more DAOS providers associated with the protocol info.
func getProviderSet(dev *crtFabricDevice, priorities providerPriorities) *hardware.FabricProviderSet {
	if dev.IsUCX() {
		// UCX determines its own priorities within the provider set
		return getProviderSetFromUCXTransport(dev.Protocol)
	}

	name := dev.ProviderName()
	return hardware.NewFabricProviderSet(&hardware.FabricProvider{
		Name:     name,
		Priority: priorities.getPriority(name),
	})
}
