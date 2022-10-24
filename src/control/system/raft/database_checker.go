//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package raft

import (
	"sync"

	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common/proto/chk"
	"github.com/daos-stack/daos/src/control/system/checker"
)

var (
	errFindingExists = errors.New("finding already exists")
)

type (
	CheckerFindingMap map[uint64]*checker.Finding

	CheckerDatabase struct {
		Findings CheckerFindingMap
	}

	// InMemCheckerDatabase is a checker.StateStore implementation that
	// does not persist data to raft, and is only suitable for ephemeral
	// checker operations.
	InMemCheckerDatabase struct {
		sync.RWMutex
		CheckerDatabase
	}
)

func copyFinding(in *checker.Finding) (out *checker.Finding) {
	out = new(checker.Finding)
	proto.Merge(&out.CheckReport, &in.CheckReport)

	return
}

func (mdb *InMemCheckerDatabase) AddCheckerFinding(finding *checker.Finding) error {
	mdb.Lock()
	defer mdb.Unlock()

	if mdb.Findings == nil {
		mdb.Findings = make(CheckerFindingMap)
	}

	if _, found := mdb.Findings[finding.Seq]; found {
		return errFindingExists
	}
	mdb.Findings[finding.Seq] = finding

	return nil
}

func (mdb *InMemCheckerDatabase) UpdateCheckerFinding(finding *checker.Finding) error {
	mdb.Lock()
	defer mdb.Unlock()

	_, found := mdb.Findings[finding.Seq]
	if !found {
		return errors.Errorf("finding 0x%x not found", finding.Seq)
	}
	// TODO: Selectively update fields?
	mdb.Findings[finding.Seq] = finding

	return nil
}

func (mdb *InMemCheckerDatabase) AddOrUpdateCheckerFinding(finding *checker.Finding) error {
	if err := mdb.AddCheckerFinding(finding); err != nil {
		if err == errFindingExists {
			return mdb.UpdateCheckerFinding(finding)
		}
		return err
	}

	return nil
}

func (mdb *InMemCheckerDatabase) GetCheckerFindings(searchList ...uint64) ([]*checker.Finding, error) {
	mdb.RLock()
	defer mdb.RUnlock()

	out := make([]*checker.Finding, 0, len(mdb.Findings))
	if len(searchList) == 0 {
		for _, finding := range mdb.Findings {
			out = append(out, copyFinding(finding))
		}
	} else {
		for _, seq := range searchList {
			finding, found := mdb.Findings[seq]
			if !found {
				return nil, errors.Errorf("finding 0x%x not found", seq)
			}
			out = append(out, copyFinding(finding))
		}
	}
	return out, nil
}

func (mdb *InMemCheckerDatabase) GetCheckerFinding(seq uint64) (*checker.Finding, error) {
	mdb.RLock()
	defer mdb.RUnlock()

	if f, found := mdb.Findings[seq]; found {
		return copyFinding(f), nil
	}

	return nil, errors.Errorf("finding 0x%x not found", seq)
}

func (mdb *InMemCheckerDatabase) SetCheckerFindingAction(seq uint64, action int32) error {
	mdb.Lock()
	defer mdb.Unlock()

	if _, ok := chk.CheckInconsistAction_name[action]; !ok {
		return errors.Errorf("invalid action %d", action)
	}
	chkAction := chk.CheckInconsistAction(action)

	if finding, found := mdb.Findings[seq]; found {
		for i, d := range finding.ActChoices {
			if d != chkAction {
				continue
			}
			finding.Action = chkAction
			if len(finding.ActMsgs) > i {
				finding.ActMsgs = []string{finding.ActMsgs[i]}
			}
			finding.ActChoices = nil
			return nil
		}
		return errors.Errorf("action %s not found", chkAction)
	}

	return errors.Errorf("finding 0x%x not found", seq)
}

func (mdb *InMemCheckerDatabase) ResetCheckerData() error {
	mdb.Lock()
	defer mdb.Unlock()

	mdb.Findings = nil

	return nil
}
