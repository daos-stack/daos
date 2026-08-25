//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"fmt"
	"strings"

	"github.com/pkg/errors"
)

const (
	// PoolCertCNPrefixNode marks a node-scoped pool cert; suffix must match
	// the local machine name.
	PoolCertCNPrefixNode = "node:"
	// PoolCertCNPrefixTenant marks a tenant-scoped pool cert shared across nodes.
	PoolCertCNPrefixTenant = "tenant:"
	// PoolCertCNSuffixMaxLen caps the portion of a pool cert CN after the
	// prefix, matching the RFC 1035 maximum DNS name length.
	PoolCertCNSuffixMaxLen = 253
)

// ParsePoolCertCN splits a pool cert CN into its prefix and suffix, rejecting
// an unknown prefix or a suffix outside the allowed character set and length.
func ParsePoolCertCN(cn string) (prefix, suffix string, err error) {
	switch {
	case strings.HasPrefix(cn, PoolCertCNPrefixNode):
		prefix = PoolCertCNPrefixNode
	case strings.HasPrefix(cn, PoolCertCNPrefixTenant):
		prefix = PoolCertCNPrefixTenant
	default:
		return "", "", fmt.Errorf("CN %q must start with %q or %q",
			cn, PoolCertCNPrefixNode, PoolCertCNPrefixTenant)
	}
	suffix = cn[len(prefix):]
	if suffix == "" {
		return "", "", fmt.Errorf("CN %q has empty suffix", cn)
	}
	if len(suffix) > PoolCertCNSuffixMaxLen {
		return "", "", fmt.Errorf("CN suffix %q exceeds max length %d",
			suffix, PoolCertCNSuffixMaxLen)
	}
	isAlnum := func(c byte) bool {
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
	}
	if !isAlnum(suffix[0]) || !isAlnum(suffix[len(suffix)-1]) {
		return "", "", fmt.Errorf("CN suffix %q must start and end with an alphanumeric", suffix)
	}
	for i := 0; i < len(suffix); i++ {
		c := suffix[i]
		switch {
		case isAlnum(c):
		case c == '.' || c == '-' || c == '_':
			if i > 0 && !isAlnum(suffix[i-1]) {
				return "", "", fmt.Errorf("CN suffix %q has consecutive separators", suffix)
			}
		default:
			return "", "", fmt.Errorf("CN suffix %q contains disallowed character %q",
				suffix, c)
		}
	}
	return prefix, suffix, nil
}

// CheckCNBinding enforces what a CN prefix binds the cert to; unknown
// prefixes are rejected.
func CheckCNBinding(prefix, suffix, machineName string) error {
	cn := prefix + suffix
	switch prefix {
	case PoolCertCNPrefixNode:
		if machineName == "" {
			return errors.Wrapf(ErrInvalidInput,
				"cert CN %q is node-scoped but machine name is empty", cn)
		}
		if suffix != machineName {
			return errors.Wrapf(ErrCertInvalid,
				"cert CN %q does not match machine name %q", cn, machineName)
		}
	case PoolCertCNPrefixTenant:
		// Deliberately unbound: one cert is shared by every machine in the tenant.
	default:
		return errors.Wrapf(ErrInvalidInput, "cert CN %q has unsupported prefix %q", cn, prefix)
	}
	return nil
}
