//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package checker

import (
	"context"

	"github.com/google/uuid"

	chkpb "github.com/daos-stack/daos/src/control/common/proto/chk"
	"github.com/daos-stack/daos/src/control/system"
)

type (
	State struct {
		Active      bool
		CurrentPass chkpb.CheckScanPhase
	}

	LogEntry struct {
		Phase   uint
		Message string
	}

	Task struct {
		Phase     uint
		Method    string
		Arguments []string
		Logs      []*LogEntry
		Next      *Task
	}

	FindingStore interface {
		AddCheckerFinding(finding *Finding) error
		GetCheckerFindings() ([]*Finding, error)
		//UpdateCheckerFinding(id string, status FindingStatus) error
	}

	Checker interface {
		RunPassChecks(ctx context.Context, pass chkpb.CheckScanPhase, db FindingStore) error
	}

	EngineCheckerReq struct {
		Pass      chkpb.CheckScanPhase
		Ranks     []system.Rank
		PoolUUIDs []uuid.UUID
	}

	EngineCheckerResult struct {
		Pass     chkpb.CheckScanPhase
		Findings []*Finding
		Pool     *system.PoolService
	}

	EngineCheckerResp struct {
		Results []*EngineCheckerResult
	}

	EngineChecker interface {
		CallEngineChecker(ctx context.Context, req *EngineCheckerReq) (*EngineCheckerResp, error)
	}
)
