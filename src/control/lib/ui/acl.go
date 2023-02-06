//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ui

import (
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/daos"
)

// ACLPrincipalFlag is a flag that represents a DAOS ACL principal.
type ACLPrincipalFlag string

func (p ACLPrincipalFlag) String() string {
	return string(p)
}

// UnmarshalFlag implements the go-flags.Unmarshaler interface.
func (p *ACLPrincipalFlag) UnmarshalFlag(fv string) error {
	pv := fv // Copy to avoid modifying the original string.
	// If necessary, try adding a trailing '@' to the string
	// in order to convert a bare user/group name to a principal.
	if !strings.ContainsRune(pv, '@') {
		pv += "@"
	}
	if !daos.ACLPrincipalIsValid(pv) {
		return errors.Errorf("invalid ACL principal %q", fv)
	}

	*p = ACLPrincipalFlag(pv)
	return nil
}
