//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package checker

import (
	"fmt"

	chkpb "github.com/daos-stack/daos/src/control/common/proto/chk"
)

type (
	Finding struct {
		chkpb.CheckReport
	}

	reportObject uint
)

const (
	poolObj reportObject = iota
	contObj
	targObj
)

func (ro reportObject) String() string {
	return map[reportObject]string{
		poolObj: "pool",
		contObj: "container",
		targObj: "target",
	}[ro]
}

func NewFinding(report *chkpb.CheckReport) *Finding {
	if report == nil {
		return nil
	}

	return &Finding{*report}
}

func descAction(action chkpb.CheckInconsistAction, ro reportObject, details ...string) string {
	// Create a map of details. If a detail is not found by
	// the expected index, then the default is an empty string.
	detMap := make(map[int]string)
	for i, det := range details {
		detMap[i] = det
	}

	switch action {
	case chkpb.CheckInconsistAction_CIA_IGNORE:
		return fmt.Sprintf("Ignore the %s finding", ro)
	case chkpb.CheckInconsistAction_CIA_DISCARD:
		return fmt.Sprintf("Discard the %q %s", detMap[0], ro)
	case chkpb.CheckInconsistAction_CIA_READD:
		return fmt.Sprintf("Re-add the %q %s", detMap[0], ro)
	case chkpb.CheckInconsistAction_CIA_TRUST_MS:
		return fmt.Sprintf("Trust the MS %s entry (%s) for %s", ro, detMap[1], detMap[0])
	case chkpb.CheckInconsistAction_CIA_TRUST_PS:
		return fmt.Sprintf("Trust the PS %s entry (%s) for %s", ro, detMap[1], detMap[0])
	case chkpb.CheckInconsistAction_CIA_TRUST_TARGET:
		return fmt.Sprintf("Trust the %s result (%s) for %s", ro, detMap[1], detMap[0])
	case chkpb.CheckInconsistAction_CIA_TRUST_MAJORITY:
		return fmt.Sprintf("Trust the majority of the %s results (%s) for %s", ro, detMap[1], detMap[0])
	case chkpb.CheckInconsistAction_CIA_TRUST_LATEST:
		return fmt.Sprintf("Trust the latest %s result (%s) for %s", ro, detMap[1], detMap[0])
	case chkpb.CheckInconsistAction_CIA_TRUST_OLDEST:
		return fmt.Sprintf("Trust the oldest %s result (%s) for %s", ro, detMap[1], detMap[0])
	case chkpb.CheckInconsistAction_CIA_TRUST_EC_PARITY:
		return fmt.Sprintf("Trust the parity of the %s results (%s) for %s", ro, detMap[1], detMap[0])
	case chkpb.CheckInconsistAction_CIA_TRUST_EC_DATA:
		return fmt.Sprintf("Trust the data of the %s results (%s) for %s", ro, detMap[1], detMap[0])
	default:
		return fmt.Sprintf("%s: %s (details: %+v)", ro, action, details)
	}
}

func AnnotateFinding(f *Finding) *Finding {
	if f == nil {
		return nil
	}

	// Pad out the list of details as necessary to match
	// the length of the action list.
	if len(f.Details) != len(f.Actions) {
		for i := len(f.Details); i < len(f.Actions); i++ {
			f.Details = append(f.Details, "")
		}
	}

	switch f.Class {
	case chkpb.CheckInconsistClass_CIC_POOL_NONEXIST_ON_MS:
		if f.Msg == "" {
			f.Msg = fmt.Sprintf("Scanned pool service %s missing from MS", f.PoolUuid)
		}
		for i, act := range f.Actions {
			f.Details[i] = descAction(act, poolObj, f.PoolUuid)
		}
	case chkpb.CheckInconsistClass_CIC_POOL_NONEXIST_ON_ENGINE:
		if f.Msg == "" {
			f.Msg = fmt.Sprintf("MS pool service %s missing on engines", f.PoolUuid)
		}
		for i, act := range f.Actions {
			f.Details[i] = descAction(act, poolObj, f.PoolUuid)
		}
	case chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL:
		if f.Msg == "" {
			f.Msg = fmt.Sprintf("The pool label for %s does not match MS", f.PoolUuid)
		}
		for i, act := range f.Actions {
			f.Details[i] = descAction(act, poolObj, f.PoolUuid, f.Details[i])
		}
	default:
		if f.Msg == "" {
			f.Msg = fmt.Sprintf("Inconsistency found: %s (details: %+v)", f.Class, f.Details)
		}
	}

	return f
}

/*func (f *Finding) SetResolution(res *FindingResolution) error {
	if res == nil {
		return errors.New("resolution is nil")
	}

	for _, r := range f.Resolutions {
		if r.Action == res.Action {
			f.Resolution = res
			return nil
		}
	}

	return errors.Errorf("resolution %d not found", res.Action)
}*/
