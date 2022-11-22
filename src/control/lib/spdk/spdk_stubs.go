//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build !spdk
// +build !spdk

// Package spdk provides Go bindings for SPDK
package spdk

import (
	"github.com/daos-stack/daos/src/control/logging"
)

// InitSPDKEnv initializes the SPDK environment.
func (ei *EnvImpl) InitSPDKEnv(log logging.Logger, opts *EnvOptions) error {
	return nil
}

// FiniSPDKEnv initializes the SPDK environment.
func (ei *EnvImpl) FiniSPDKEnv(log logging.Logger, opts *EnvOptions) {
	return
}
