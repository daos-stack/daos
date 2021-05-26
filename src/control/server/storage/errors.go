//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import "github.com/pkg/errors"

var (
	ErrInvalidDcpmCount = errors.New("expected exactly 1 DCPM device")
	ErrNoScmTiers       = errors.New("expected exactly 1 SCM tier in storage configuration")
)
