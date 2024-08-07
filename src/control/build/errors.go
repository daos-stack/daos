//
// (C) Copyright 2022-2024 Intel Corporation.
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

var (
	// ErrNoCtxMetadata is returned when no component/version metadata is found in the context.
	ErrNoCtxMetadata = errors.New("no component/version metadata found in context")
	// ErrCtxMetadataExists is returned when component/version metadata has already been set in the context.
	ErrCtxMetadataExists = errors.New("component/version metadata already exists in context")
)
