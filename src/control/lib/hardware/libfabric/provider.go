//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package libfabric

import (
	"context"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
)

// NewProvider creates a new libfabric data provider.
func NewProvider(log logging.Logger) *Provider {
	return &Provider{
		log: log,
	}
}

// Provider provides information from libfabric's API.
type Provider struct {
	log logging.Logger
}

// GetFabricInterfaces harvests the collection of fabric interfaces from libfabric.
func (p *Provider) GetFabricInterfaces(ctx context.Context) (*hardware.FabricInterfaceSet, error) {
	ch := make(chan *fabricResult)
	go p.getFabricInterfaces(ch)
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

func (p *Provider) getFabricInterfaces(ch chan *fabricResult) {
	hdl, err := openLib()
	if err != nil {
		ch <- &fabricResult{
			err: err,
		}
		return
	}
	defer hdl.Close()

	fiInfo, cleanup, err := fiGetInfo(hdl)
	if err != nil {
		ch <- &fabricResult{
			err: err,
		}
		return
	}
	defer func() {
		if err := cleanup(); err != nil {
			p.log.Errorf("unable to clean up fi_info: %s", err.Error())
		}
	}()

	fis := hardware.NewFabricInterfaceSet()

	for _, info := range fiInfo {
		newFI, err := p.infoToFabricInterface(info)
		if err != nil {
			p.log.Error(err.Error())
			continue
		}
		fis.Update(newFI)
	}

	p.log.Debugf("found fabric interfaces:\n%s", fis)

	ch <- &fabricResult{
		fiSet: fis,
	}
}

type info interface {
	domainName() string
	fabricProvider() string
}

func (p *Provider) infoToFabricInterface(fi info) (*hardware.FabricInterface, error) {
	if fi == nil {
		return nil, errors.New("nil FI info")
	}

	name := fi.domainName()
	if name == "" {
		return nil, errors.New("libfabric info has no domain name")
	}

	lfProvider := fi.fabricProvider()
	extProvider, err := libFabricProviderListToExt(lfProvider)
	if err != nil {
		return nil, errors.Errorf("failed to parse provider %q: %s", lfProvider, err.Error())
	}

	newFI := &hardware.FabricInterface{
		Name:      name,
		OSName:    name,
		Providers: common.NewStringSet(extProvider),
	}
	return newFI, nil
}

// libFabricProviderToExt converts a single libfabric provider string into a DAOS provider string
func libFabricProviderToExt(provider string) string {
	switch provider {
	case "sockets", "tcp", "verbs", "psm2", "gni", "cxi":
		return "ofi+" + provider
	default:
		return provider
	}
}

// libFabricProviderListToExt converts a libfabric provider string containing one or more providers
// separated by ';' into a DAOS compatible provider string.
func libFabricProviderListToExt(providerList string) (string, error) {
	var result string

	trimmedList := strings.TrimSpace(providerList)

	if len(trimmedList) == 0 {
		return "", errors.New("provider list was empty")
	}

	providers := strings.Split(providerList, ";")
	for _, subProvider := range providers {
		subProvider = strings.TrimSpace(subProvider)
		if subProvider == "" {
			return "", errors.Errorf("malformed provider list %q", providerList)
		}
		result += libFabricProviderToExt(subProvider) + ";"
	}

	return strings.TrimSuffix(result, ";"), nil
}
