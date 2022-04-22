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

func (db *CheckerDatabase) copyFinding(in *checker.Finding) (out *checker.Finding) {
	out = new(checker.Finding)
	proto.Merge(&out.CheckReport, &in.CheckReport)

	return
}

func (mdb *InMemCheckerDatabase) AddCheckerFinding(finding *checker.Finding) error {
	mdb.Lock()
	defer mdb.Unlock()

	mdb.Findings = append(mdb.Findings, finding)

	return nil
}

func (mdb *InMemCheckerDatabase) GetCheckerFindings() ([]*checker.Finding, error) {
	mdb.RLock()
	defer mdb.RUnlock()

	out := make([]*checker.Finding, len(mdb.Findings))
	for i, finding := range mdb.Findings {
		out[i] = mdb.copyFinding(finding)
	}
	return out, nil
}

func (mdb *InMemCheckerDatabase) GetCheckerFinding(seq uint64) (*checker.Finding, error) {
	mdb.RLock()
	defer mdb.RUnlock()

	for _, finding := range mdb.Findings {
		if finding.Seq == seq {
			return mdb.copyFinding(finding), nil
		}
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

	for _, finding := range mdb.Findings {
		if finding.Seq == seq {
			for i, d := range finding.ActChoices {
				if d == chkAction {
					finding.Action = chkAction
					if len(finding.ActMsgs) > i {
						finding.ActMsgs = []string{finding.ActMsgs[i]}
					}
					finding.ActChoices = nil
					break
				}
				return errors.Errorf("action %s not found", chkAction)
			}
			return nil
		}
	}

	return errors.Errorf("finding 0x%x not found", seq)
}

func (mdb *InMemCheckerDatabase) ResetCheckerData() error {
	mdb.Lock()
	defer mdb.Unlock()

	mdb.Findings = nil

	return nil
}
