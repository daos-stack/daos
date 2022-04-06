//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package raft

import (
	"sync"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/system/checker"
)

type (
	CheckerDatabase struct {
		Findings []*checker.Finding
	}

	// InMemCheckerDatabase is a checker.StateStore implementation that
	// does not persist data to raft, and is only suitable for ephemeral
	// checker operations.
	InMemCheckerDatabase struct {
		sync.RWMutex
		CheckerDatabase
	}
)

func (mdb *InMemCheckerDatabase) AddCheckerFinding(finding *checker.Finding) error {
	mdb.Lock()
	defer mdb.Unlock()

	mdb.Findings = append(mdb.Findings, finding)

	return nil
}

func (mdb *InMemCheckerDatabase) GetCheckerFindings() ([]*checker.Finding, error) {
	mdb.RLock()
	defer mdb.RUnlock()

	return mdb.Findings, nil
}

func (mdb *InMemCheckerDatabase) UpdateCheckerFinding(f *checker.Finding) error {
	mdb.Lock()
	defer mdb.Unlock()

	// For the moment, just update with a subset.
	for _, finding := range mdb.Findings {
		if finding.Seq == f.Seq {
			finding.Action = f.Action
			finding.Actions = f.Actions
			finding.Details = f.Details
			return nil
		}
	}

	return errors.Errorf("finding 0x%x not found", f.Seq)
}

func (mdb *InMemCheckerDatabase) ResetCheckerData() error {
	mdb.Lock()
	defer mdb.Unlock()

	mdb.Findings = nil

	return nil
}
