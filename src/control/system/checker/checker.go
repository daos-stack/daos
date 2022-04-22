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
		GetCheckerFinding(seq uint64) (*Finding, error)
		SetCheckerFindingAction(seq uint64, action int32) error
	}
)
