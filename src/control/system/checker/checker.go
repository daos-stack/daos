//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package checker

type (
	FindingStore interface {
		AddCheckerFinding(finding *Finding) error
		UpdateCheckerFinding(finding *Finding) error
		AddOrUpdateCheckerFinding(finding *Finding) error
		GetCheckerFindings(seqs ...uint64) ([]*Finding, error)
		GetCheckerFinding(seq uint64) (*Finding, error)
		SetCheckerFindingAction(seq uint64, action int32) error
	}
)
