//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package cmdutil

import (
	"testing"
	"time"
)

func TestCmdutil_CertExpiryWarning(t *testing.T) {
	now := time.Date(2026, 10, 6, 12, 0, 0, 0, time.UTC)
	for name, tc := range map[string]struct {
		notAfter time.Time
		exp      string
	}{
		"expired":        {now.Add(-time.Hour), "CA expired on 2026-10-06"},
		"within window":  {now.Add(10*24*time.Hour + time.Hour), "CA expires in 10 day(s), on 2026-10-16"},
		"outside window": {now.Add(CertExpiryWarnWindow + time.Hour), ""},
	} {
		t.Run(name, func(t *testing.T) {
			if got := CertExpiryWarning("CA", tc.notAfter, now); got != tc.exp {
				t.Fatalf("got %q, want %q", got, tc.exp)
			}
		})
	}
}
