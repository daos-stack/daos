//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package checker

import "github.com/pkg/errors"

var (
	ErrNoMorePasses = errors.New("no more passes")
	ErrPassFindings = errors.New("pass has findings that must be addressed")
)

func IsPassFindings(err error) bool {
	return errors.Cause(err) == ErrPassFindings
}
