//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"strings"
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
)

func TestSecurity_ParsePoolCertCN(t *testing.T) {
	for name, tc := range map[string]struct {
		cn        string
		expPrefix string
		expSuffix string
		expErr    error
	}{
		"node":                {cn: "node:host1", expPrefix: PoolCertCNPrefixNode, expSuffix: "host1"},
		"tenant":              {cn: "tenant:teamA", expPrefix: PoolCertCNPrefixTenant, expSuffix: "teamA"},
		"fqdn":                {cn: "node:host1.example.com", expPrefix: PoolCertCNPrefixNode, expSuffix: "host1.example.com"},
		"underscore":          {cn: "tenant:team_A", expPrefix: PoolCertCNPrefixTenant, expSuffix: "team_A"},
		"no prefix":           {cn: "host1", expErr: errors.New("must start with")},
		"empty node suffix":   {cn: "node:", expErr: errors.New("empty suffix")},
		"empty tenant suffix": {cn: "tenant:", expErr: errors.New("empty suffix")},
		"null byte":           {cn: "node:host\x00evil", expErr: errors.New("disallowed character")},
		"newline":             {cn: "node:host\nhostile", expErr: errors.New("disallowed character")},
		"colon":               {cn: "node:admin:bypass", expErr: errors.New("disallowed character")},
		"space":               {cn: "node:host one", expErr: errors.New("disallowed character")},
		"leading dot":         {cn: "node:.host", expErr: errors.New("must start and end with an alphanumeric")},
		"trailing dot":        {cn: "node:host.", expErr: errors.New("must start and end with an alphanumeric")},
		"leading hyphen":      {cn: "node:-host", expErr: errors.New("must start and end with an alphanumeric")},
		"consecutive dots":    {cn: "node:host..example", expErr: errors.New("consecutive separators")},
		"consecutive hyphens": {cn: "node:host--example", expErr: errors.New("consecutive separators")},
		"too long":            {cn: "node:" + strings.Repeat("a", poolCertCNSuffixMaxLen+1), expErr: errors.New("exceeds max length")},
	} {
		t.Run(name, func(t *testing.T) {
			prefix, suffix, err := ParsePoolCertCN(tc.cn)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, tc.expPrefix, prefix, "prefix")
			test.AssertEqual(t, tc.expSuffix, suffix, "suffix")
		})
	}
}

func TestSecurity_checkCNBinding(t *testing.T) {
	for name, tc := range map[string]struct {
		prefix  string
		suffix  string
		machine string
		expErr  error
	}{
		"node matches":       {prefix: PoolCertCNPrefixNode, suffix: "machine1", machine: "machine1"},
		"node mismatch":      {prefix: PoolCertCNPrefixNode, suffix: "machine1", machine: "machine2", expErr: ErrCertInvalid},
		"node no machine":    {prefix: PoolCertCNPrefixNode, suffix: "machine1", expErr: ErrInvalidInput},
		"tenant any machine": {prefix: PoolCertCNPrefixTenant, suffix: "teamA", machine: "machine2"},
		"tenant no machine":  {prefix: PoolCertCNPrefixTenant, suffix: "teamA"},
		"unknown prefix":     {prefix: "service:", suffix: "x", machine: "x", expErr: ErrInvalidInput},
	} {
		t.Run(name, func(t *testing.T) {
			test.CmpErr(t, tc.expErr, checkCNBinding(tc.prefix, tc.suffix, tc.machine))
		})
	}
}
