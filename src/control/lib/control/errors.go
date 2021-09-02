//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import "github.com/pkg/errors"

var (
	// ErrNoConfigFile indicates that no configuration file was able
	// to be located.
	ErrNoConfigFile = errors.New("no configuration file found")
)
