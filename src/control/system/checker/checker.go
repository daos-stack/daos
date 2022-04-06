//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package checker

type (
	FindingStore interface {
		AddCheckerFinding(finding *Finding) error
		GetCheckerFindings() ([]*Finding, error)
		UpdateCheckerFinding(finding *Finding) error
	}
)
