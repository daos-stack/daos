//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ui

import (
	"strings"

	"github.com/pkg/errors"
)

// EnabledFlag allows a flag to be optionally set to a boolean value.
type EnabledFlag struct {
	Set     bool
	Enabled bool
}

// UnmarshalFlag implements the flags.Unmarshaler interface.
func (f *EnabledFlag) UnmarshalFlag(fv string) error {
	f.Set = true

	switch strings.ToLower(fv) {
	case "true", "1", "yes", "on":
		f.Enabled = true
	case "false", "0", "no", "off":
		f.Enabled = false
	default:
		return errors.Errorf("invalid boolean value %q", fv)
	}

	return nil
}
