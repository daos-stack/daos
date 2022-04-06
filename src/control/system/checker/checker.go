//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package checker

import (
	"context"

	"github.com/pkg/errors"
)

type (
	Pass uint

	State struct {
		Active      bool
		CurrentPass Pass
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
		UpdateCheckerFinding(id string, status FindingStatus) error
	}

	Checker interface {
		RunPassChecks(ctx context.Context, pass Pass, db FindingStore) error
	}
)

const (
	PassInactive Pass = iota
	PassInit
	PassPoolList
	PassPoolMembers
	PassPoolCleanup
	PassContainerList
	PassContainerCleanup
	PassObjectScrub
	PassObjectConsistency
	MaxPass
)

func (cp Pass) String() string {
	switch cp {
	case PassInactive:
		return "Inactive"
	case PassInit:
		return "Init"
	case PassPoolList:
		return "Pool List"
	case PassPoolMembers:
		return "Pool Membership"
	case PassPoolCleanup:
		return "Pool Cleanup"
	case PassContainerList:
		return "Container List"
	case PassContainerCleanup:
		return "Container Cleanup"
	case PassObjectScrub:
		return "Object Scrub"
	case PassObjectConsistency:
		return "Object Consistency"
	}
	return "Unknown"
}

func (cp *Pass) FromString(s string) error {
	switch s {
	case "Inactive":
		*cp = PassInactive
	case "Init":
		*cp = PassInit
	case "Pool List":
		*cp = PassPoolList
	case "Pool Membership":
		*cp = PassPoolMembers
	case "Pool Cleanup":
		*cp = PassPoolCleanup
	case "Container List":
		*cp = PassContainerList
	case "Container Cleanup":
		*cp = PassContainerCleanup
	case "Object Scrub":
		*cp = PassObjectScrub
	case "Object Consistency":
		*cp = PassObjectConsistency
	default:
		return errors.Errorf("unknown pass %q", s)
	}
	return nil
}
