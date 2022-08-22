//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hardware

import (
	"fmt"

	"github.com/pkg/errors"
)

// ErrNoNUMANodes indicates that the system can't detect any NUMA nodes.
var ErrNoNUMANodes = errors.New("no NUMA nodes detected")

// IsUnsupportedFabric returns true if the supplied error is
// an instance of errUnsupportedFabric.
func IsUnsupportedFabric(err error) bool {
	_, ok := errors.Cause(err).(*errUnsupportedFabric)
	return ok
}

type errUnsupportedFabric struct {
	provider string
}

func (euf *errUnsupportedFabric) Error() string {
	return fmt.Sprintf("fabric provider %q not supported", euf.provider)
}

// ErrUnsupportedFabric returns an error indicating that the denoted
// fabric provider is not supported by this build/platform.
func ErrUnsupportedFabric(provider string) error {
	return &errUnsupportedFabric{provider: provider}
}

// IsProviderNotOnDevice indicates whether the error is an instance of
// errProviderNotOnDevice.
func IsProviderNotOnDevice(err error) bool {
	_, ok := errors.Cause(err).(*errProviderNotOnDevice)
	return ok
}

type errProviderNotOnDevice struct {
	provider string
	device   string
}

func (e *errProviderNotOnDevice) Error() string {
	return fmt.Sprintf("fabric provider %q not supported on network device %q", e.provider, e.device)
}

// ErrProviderNotOnDevice returns an error indicated that the fabric provider
// is not available on the given network device.
func ErrProviderNotOnDevice(provider, dev string) error {
	return &errProviderNotOnDevice{
		provider: provider,
		device:   dev,
	}
}
