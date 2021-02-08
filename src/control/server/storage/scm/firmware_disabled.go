//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
// +build !firmware

package scm

import (
	"github.com/daos-stack/daos/src/control/logging"
)

// firmwareProvider does nothing if firmware operations are not enabled.
type firmwareProvider struct{}

// setupFirmwareProvider does nothing if firmware operations are not enabled.
func (p *Provider) setupFirmwareProvider(log logging.Logger) {}
