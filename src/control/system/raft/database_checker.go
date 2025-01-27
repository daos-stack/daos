//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package raft

import (
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/chk"
	"github.com/daos-stack/daos/src/control/system/checker"
)

var (
	errFindingExists = errors.New("finding already exists")
)

type (
	errFindingNotFound struct {
		seq uint64
	}

	// CheckerFindingMap allows the lookup of a Finding by its sequence number.
	CheckerFindingMap map[uint64]*checker.Finding

	// CheckerDatabase is the database containing all checker Findings.
	CheckerDatabase struct {
		Findings CheckerFindingMap
	}
)

func (e *errFindingNotFound) Error() string {
	return errors.Errorf("finding 0x%x not found", e.seq).Error()
}

// ErrFindingNotFound creates an error that indicates a specified finding wasn't found in the
// database.
func ErrFindingNotFound(seq uint64) error {
	return &errFindingNotFound{seq: seq}
}

// IsFindingNotFound checks whether an error is an ErrFindingNotFound.
func IsFindingNotFound(err error) bool {
	_, ok := errors.Cause(err).(*errFindingNotFound)
	return ok
}

func copyFinding(in *checker.Finding) (out *checker.Finding) {
	out = new(checker.Finding)
	proto.Merge(&out.CheckReport, &in.CheckReport)

	return
}

func (cdb *CheckerDatabase) resetFindings() {
	cdb.Findings = make(CheckerFindingMap)
}

func (cdb *CheckerDatabase) addFinding(finding *checker.Finding) error {
	if _, found := cdb.Findings[finding.Seq]; found {
		return errFindingExists
	}
	cdb.Findings[finding.Seq] = copyFinding(finding)

	return nil
}

func (cdb *CheckerDatabase) updateFinding(finding *checker.Finding) error {
	if _, found := cdb.Findings[finding.Seq]; !found {
		return ErrFindingNotFound(finding.Seq)
	}
	// TODO: Selectively update fields?
	cdb.Findings[finding.Seq] = finding

	return nil
}

func (cdb *CheckerDatabase) removeFinding(finding *checker.Finding) error {
	if _, found := cdb.Findings[finding.Seq]; !found {
		return ErrFindingNotFound(finding.Seq)
	}

	delete(cdb.Findings, finding.Seq)
	return nil
}

// AddCheckerFinding adds a finding to the database.
func (db *Database) AddCheckerFinding(finding *checker.Finding) error {
	db.Lock()
	defer db.Unlock()

	return db.submitCheckerUpdate(raftOpAddCheckerFinding, finding)
}

// AddOrUpdateCheckerFinding updates a finding in the database if it is already stored, or stores
// it if not.
func (db *Database) AddOrUpdateCheckerFinding(finding *checker.Finding) error {
	db.Lock()
	defer db.Unlock()

	if _, err := db.GetCheckerFinding(finding.Seq); IsFindingNotFound(err) {
		return db.submitCheckerUpdate(raftOpAddCheckerFinding, finding)
	}

	return db.submitCheckerUpdate(raftOpUpdateCheckerFinding, finding)
}

// UpdateCheckerFinding updates a finding that is already in the database.
func (db *Database) UpdateCheckerFinding(finding *checker.Finding) error {
	db.Lock()
	defer db.Unlock()

	if _, err := db.GetCheckerFinding(finding.Seq); err != nil {
		return err
	}
	return db.submitCheckerUpdate(raftOpUpdateCheckerFinding, finding)
}

// RemoveCheckerFindingsForPools removes any findings in the database associated with one or more
// pool IDs.
func (db *Database) RemoveCheckerFindingsForPools(poolIDs ...string) error {
	db.Lock()
	defer db.Unlock()

	poolIDSet := common.NewStringSet(poolIDs...)
	for seq, f := range db.data.Checker.Findings {
		if poolIDSet.Has(f.PoolUuid) || poolIDSet.Has(f.PoolLabel) {
			delete(db.data.Checker.Findings, seq)
		}
	}
	return nil
}

// RemoveCheckerFinding removes a given finding from the checker database.
func (db *Database) RemoveCheckerFinding(finding *checker.Finding) error {
	db.Lock()
	defer db.Unlock()

	if _, err := db.GetCheckerFinding(finding.Seq); err != nil {
		return err
	}
	return db.submitCheckerUpdate(raftOpRemoveCheckerFinding, finding)
}

// SetCheckerFindingAction sets the action chosen for a giving finding.
func (db *Database) SetCheckerFindingAction(seq uint64, action int32) error {
	if _, ok := chk.CheckInconsistAction_name[action]; !ok {
		return errors.Errorf("invalid action %d", action)
	}
	chkAction := chk.CheckInconsistAction(action)

	db.Lock()
	defer db.Unlock()

	f, err := db.GetCheckerFinding(seq)
	if err != nil {
		return err
	}

	for i, d := range f.ActChoices {
		if d != chkAction {
			continue
		}
		f.Action = chkAction
		if len(f.ActMsgs) > i {
			f.ActMsgs = []string{f.ActMsgs[i]}
		}
		f.ActChoices = nil
	}

	return db.submitCheckerUpdate(raftOpUpdateCheckerFinding, f)
}

// ResetCheckerData clears all findings in the database.
func (db *Database) ResetCheckerData() error {
	db.Lock()
	defer db.Unlock()

	return db.submitCheckerUpdate(raftOpClearCheckerFindings, nil)
}

// GetCheckerFindings fetches findings from the database by sequence number, or fetches all of them
// if no list is provided.
func (db *Database) GetCheckerFindings(searchList ...uint64) ([]*checker.Finding, error) {
	db.data.RLock()
	defer db.data.RUnlock()

	out := make([]*checker.Finding, 0, len(db.data.Checker.Findings))
	if len(searchList) == 0 {
		for _, finding := range db.data.Checker.Findings {
			out = append(out, copyFinding(finding))
		}
	} else {
		for _, seq := range searchList {
			finding, found := db.data.Checker.Findings[seq]
			if !found {
				return nil, errors.Errorf("finding 0x%x not found", seq)
			}
			out = append(out, copyFinding(finding))
		}
	}
	return out, nil
}

// GetCheckerFinding looks up a finding by sequence number.
func (db *Database) GetCheckerFinding(seq uint64) (*checker.Finding, error) {
	db.data.RLock()
	defer db.data.RUnlock()

	if f, found := db.data.Checker.Findings[seq]; found {
		return copyFinding(f), nil
	}

	return nil, ErrFindingNotFound(seq)
}
