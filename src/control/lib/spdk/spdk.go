//
// (C) Copyright 2018-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// Package spdk provides Go bindings for SPDK
package spdk

import (
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
)

// Env is the interface that provides SPDK environment management.
type Env interface {
	InitSPDKEnv(logging.Logger, *EnvOptions) error
	FiniSPDKEnv(logging.Logger, *EnvOptions)
}

// EnvImpl is a an implementation of the Env interface.
type EnvImpl struct{}

// EnvOptions describe parameters to be used when initializing a processes
// SPDK environment.
type EnvOptions struct {
	PCIAllowList *hardware.PCIAddressSet // restrict SPDK device access
	EnableVMD    bool                    // flag if VMD functionality should be enabled
}

func (eo *EnvOptions) sanitizeAllowList() error {
	if eo == nil {
		return errors.New("nil EnvOptions")
	}
	if eo.PCIAllowList == nil {
		return errors.New("nil EnvOptions.PCIAllowList")
	}

	// DPDK will not accept VMD backing device addresses so convert to VMD addresses
	newSet, err := eo.PCIAllowList.BackingToVMDAddresses()
	if err != nil {
		return err
	}
	eo.PCIAllowList = newSet

	return nil
}
