//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package build

import (
	"fmt"
	"strings"

	"github.com/pkg/errors"
)

// ErrIncompatComponents is returned when two components are incompatible.
type ErrIncompatComponents struct {
	Components []*VersionedComponent
}

func (e ErrIncompatComponents) Error() string {
	compStrs := make([]string, len(e.Components))
	for i, c := range e.Components {
		compStrs[i] = fmt.Sprintf("%s:%s", c.Component, c.Version)
	}
	return fmt.Sprintf("incompatible components: %s", strings.Join(compStrs, ", "))
}

func errIncompatComponents(components ...*VersionedComponent) error {
	return ErrIncompatComponents{components}
}

// IsIncompatComponents returns true if the error is an instance of ErrIncompatComponents.
func IsIncompatComponents(err error) bool {
	_, ok := errors.Cause(err).(ErrIncompatComponents)
	return ok
}
