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
)

func TestSecurity_ParsePoolCertCN(t *testing.T) {
	for name, tc := range map[string]struct {
		cn         string
		wantPrefix string
		wantSuffix string
		wantErr    string
	}{
		"node ok":             {cn: "node:host1", wantPrefix: "node:", wantSuffix: "host1"},
		"tenant ok":           {cn: "tenant:teamA", wantPrefix: "tenant:", wantSuffix: "teamA"},
		"fqdn ok":             {cn: "node:host1.example.com", wantPrefix: "node:", wantSuffix: "host1.example.com"},
		"underscore ok":       {cn: "tenant:team_A", wantPrefix: "tenant:", wantSuffix: "team_A"},
		"no prefix":           {cn: "host1", wantErr: "must start with"},
		"empty suffix node":   {cn: "node:", wantErr: "empty suffix"},
		"empty suffix tenant": {cn: "tenant:", wantErr: "empty suffix"},
		"embedded null byte":  {cn: "node:host\x00evil", wantErr: "disallowed character"},
		"embedded newline":    {cn: "node:host\nhostile", wantErr: "disallowed character"},
		"embedded colon":      {cn: "node:admin:bypass", wantErr: "disallowed character"},
		"space rejected":      {cn: "node:host one", wantErr: "disallowed character"},
		"leading dot":         {cn: "node:.host", wantErr: "must start and end with an alphanumeric"},
		"trailing dot":        {cn: "node:host.", wantErr: "must start and end with an alphanumeric"},
		"leading hyphen":      {cn: "node:-host", wantErr: "must start and end with an alphanumeric"},
		"consecutive dots":    {cn: "node:host..example", wantErr: "consecutive separators"},
		"consecutive hyphens": {cn: "node:host--example", wantErr: "consecutive separators"},
		"too long": {
			cn:      "node:" + strings.Repeat("a", PoolCertCNSuffixMaxLen+1),
			wantErr: "exceeds max length",
		},
	} {
		t.Run(name, func(t *testing.T) {
			prefix, suffix, err := ParsePoolCertCN(tc.cn)
			if tc.wantErr != "" {
				if err == nil {
					t.Fatalf("expected error containing %q, got nil", tc.wantErr)
				}
				if !strings.Contains(err.Error(), tc.wantErr) {
					t.Fatalf("expected error containing %q, got %q", tc.wantErr, err.Error())
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if prefix != tc.wantPrefix || suffix != tc.wantSuffix {
				t.Fatalf("got (%q, %q), want (%q, %q)",
					prefix, suffix, tc.wantPrefix, tc.wantSuffix)
			}
		})
	}
}

func TestSecurity_CheckCNBinding(t *testing.T) {
	for name, tc := range map[string]struct {
		prefix, suffix, machine string
		expErr                  error
	}{
		"node cert bound to its machine": {
			prefix: PoolCertCNPrefixNode, suffix: "machine1", machine: "machine1",
		},
		"node cert on the wrong machine": {
			prefix: PoolCertCNPrefixNode, suffix: "machine1", machine: "machine2",
			expErr: ErrCertInvalid,
		},
		"node cert with no machine name to bind to": {
			prefix: PoolCertCNPrefixNode, suffix: "machine1", machine: "",
			expErr: ErrInvalidInput,
		},
		"tenant cert is not machine-bound": {
			prefix: PoolCertCNPrefixTenant, suffix: "teamA", machine: "machine2",
		},
		"tenant cert needs no machine name": {
			prefix: PoolCertCNPrefixTenant, suffix: "teamA", machine: "",
		},
		// Only reachable if ValidatePoolCertCN ever grows a prefix this
		// function was not taught about; it must fail closed.
		"unknown prefix is rejected": {
			prefix: "service:", suffix: "x", machine: "x",
			expErr: ErrInvalidInput,
		},
	} {
		t.Run(name, func(t *testing.T) {
			err := CheckCNBinding(tc.prefix, tc.suffix, tc.machine)
			if tc.expErr == nil {
				if err != nil {
					t.Fatalf("unexpected error: %v", err)
				}
				return
			}
			if !errors.Is(err, tc.expErr) {
				t.Fatalf("expected %v, got %v", tc.expErr, err)
			}
		})
	}
}
