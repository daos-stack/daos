//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package raft

import (
	"sync"

	"github.com/daos-stack/daos/src/control/system/checker"
)

var (
	_ checker.StateStore = (*InMemCheckerDatabase)(nil)
)

type (
	CheckerDatabase struct {
		State    checker.State
		Tasks    []*checker.Task
		Findings []*checker.Finding
		Logs     []*checker.LogEntry
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

	/*if finding.ID == uuid.Nil.String() {
		finding.ID = uuid.New().String()
	}*/

	mdb.Findings = append(mdb.Findings, finding)

	return nil
}

func (mdb *InMemCheckerDatabase) GetCheckerFindings() ([]*checker.Finding, error) {
	mdb.RLock()
	defer mdb.RUnlock()

	return mdb.Findings, nil
}

/*func (mdb *InMemCheckerDatabase) UpdateCheckerFinding(id string, status checker.FindingStatus) error {
	mdb.Lock()
	defer mdb.Unlock()

	for _, finding := range mdb.Findings {
		if finding.ID == id {
			return finding.SetStatus(status)
		}
	}

	return errors.Errorf("finding %s not found", id)
}*/

func (mdb *InMemCheckerDatabase) ResetCheckerData() error {
	mdb.Lock()
	defer mdb.Unlock()

	mdb.State.CurrentPass = 0
	mdb.Findings = nil
	mdb.Logs = nil
	mdb.Tasks = nil

	return nil
}

func (mdb *InMemCheckerDatabase) GetCheckerState() (checker.State, error) {
	mdb.RLock()
	defer mdb.RUnlock()

	return mdb.State, nil
}

/*func (mdb *InMemCheckerDatabase) AdvanceCheckerPass() (checker.Pass, error) {
	mdb.Lock()
	defer mdb.Unlock()

	mdb.State.CurrentPass++
	var err error
	if mdb.State.CurrentPass >= checker.MaxPass {
		mdb.State.CurrentPass = checker.PassInactive
		err = checker.ErrNoMorePasses
	}
	return mdb.State.CurrentPass, err
}*/
