//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package checker

import (
	"fmt"
	"strconv"
	"strings"

	chkpb "github.com/daos-stack/daos/src/control/common/proto/chk"
	"google.golang.org/protobuf/proto"
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

func (f *Finding) HasChoice(action chkpb.CheckInconsistAction) bool {
	for _, a := range f.ActChoices {
		if a == action {
			return true
		}
	}
	return false
}

func (f *Finding) ValidChoicesString() string {
	if len(f.ActChoices) == 0 {
		return "no valid action choices (already repaired?)"
	}

	var actions []string
	for _, a := range f.ActChoices {
		actions = append(actions, strconv.Itoa(int(a)))
	}
	return strings.Join(actions, ",")
}

func NewFinding(report *chkpb.CheckReport) *Finding {
	if report == nil {
		return nil
	}

	f := new(Finding)
	proto.Merge(&f.CheckReport, report)
	return f
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
	if len(f.ActDetails) != len(f.ActChoices) {
		for i := len(f.ActDetails); i < len(f.ActChoices); i++ {
			f.ActDetails = append(f.ActDetails, "")
		}
	}
	f.ActMsgs = make([]string, len(f.ActChoices))

	switch f.Class {
	case chkpb.CheckInconsistClass_CIC_POOL_NONEXIST_ON_MS:
		if f.Msg == "" {
			f.Msg = fmt.Sprintf("Scanned pool service %s missing from MS", f.PoolUuid)
		}
		for i, act := range f.ActChoices {
			f.ActMsgs[i] = descAction(act, poolObj, f.PoolUuid)
		}
	case chkpb.CheckInconsistClass_CIC_POOL_NONEXIST_ON_ENGINE:
		if f.Msg == "" {
			f.Msg = fmt.Sprintf("MS pool service %s missing on engines", f.PoolUuid)
		}
		for i, act := range f.ActChoices {
			f.ActMsgs[i] = descAction(act, poolObj, f.PoolUuid)
		}
	case chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL:
		if f.Msg == "" {
			f.Msg = fmt.Sprintf("The pool label for %s does not match MS", f.PoolUuid)
		}
		for i, act := range f.ActChoices {
			f.ActMsgs[i] = descAction(act, poolObj, f.PoolUuid, f.ActDetails[i])
		}
	default:
		if f.Msg == "" {
			f.Msg = fmt.Sprintf("Inconsistency found: %s (details: %+v)", f.Class, f.ActDetails)
		}
	}

	return f
}
