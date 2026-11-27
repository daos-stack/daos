//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package cmdutil

import (
	"fmt"
	"time"
)

// CertExpiryWarnWindow is how far ahead of NotAfter CertExpiryWarning reports.
const CertExpiryWarnWindow = 30 * 24 * time.Hour

// CertExpiryWarning describes a certificate that is expired or expires
// within CertExpiryWarnWindow; it returns "" otherwise.
func CertExpiryWarning(what string, notAfter, now time.Time) string {
	left := notAfter.Sub(now)
	switch {
	case left < 0:
		return fmt.Sprintf("%s expired on %s", what, notAfter.Format("2006-01-02"))
	case left < CertExpiryWarnWindow:
		return fmt.Sprintf("%s expires in %d day(s), on %s", what,
			int(left.Hours()/24), notAfter.Format("2006-01-02"))
	}
	return ""
}
