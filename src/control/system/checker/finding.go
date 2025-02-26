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
	"google.golang.org/protobuf/reflect/protoreflect"
)

type (
	Finding struct {
		chkpb.CheckReport
	}

	reportObject uint
)

const (
	unkObj reportObject = iota
	poolObj
	contObj
	engObj
	otherObj
)

func (ro reportObject) String() string {
	return map[reportObject]string{
		unkObj:   "unknown",
		poolObj:  "pool",
		contObj:  "container",
		engObj:   "target",
		otherObj: "other",
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

// descAction attempts to generate a human-readable description of the
// action that may be taken for the given finding.
func descAction(class chkpb.CheckInconsistClass, action chkpb.CheckInconsistAction, details ...string) string {
	var ro reportObject
	switch {
	case class >= chkpb.CheckInconsistClass_CIC_POOL_LESS_SVC_WITH_QUORUM && class <= chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL:
		ro = poolObj
	case class >= chkpb.CheckInconsistClass_CIC_CONT_NONEXIST_ON_PS && class <= chkpb.CheckInconsistClass_CIC_CONT_BAD_LABEL:
		ro = contObj
	case class >= chkpb.CheckInconsistClass_CIC_ENGINE_NONEXIST_IN_MAP && class <= chkpb.CheckInconsistClass_CIC_ENGINE_HAS_NO_STORAGE:
		ro = engObj
	default:
		ro = otherObj
	}

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
		return fmt.Sprintf("Discard the %s", ro)
	case chkpb.CheckInconsistAction_CIA_READD:
		return fmt.Sprintf("Re-add the %s", ro)
	case chkpb.CheckInconsistAction_CIA_TRUST_MS:
		switch ro {
		case poolObj:
			switch class {
			case chkpb.CheckInconsistClass_CIC_POOL_NONEXIST_ON_MS:
				return fmt.Sprintf("Reclaim the orphaned pool storage for %s", detMap[0])
			case chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL:
				return fmt.Sprintf("Reset the pool property using the MS label for %s", detMap[0])
			}
			return fmt.Sprintf("Trust the MS pool entry for %s", detMap[0])
		}
		return fmt.Sprintf("Trust the MS %s entry", ro)
	case chkpb.CheckInconsistAction_CIA_TRUST_PS:
		switch ro {
		case poolObj:
			switch class {
			case chkpb.CheckInconsistClass_CIC_POOL_NONEXIST_ON_MS:
				return fmt.Sprintf("Recreate the MS pool entry for %s", detMap[0])
			case chkpb.CheckInconsistClass_CIC_POOL_NONEXIST_ON_ENGINE:
				return fmt.Sprintf("Remove the MS pool entry for %s", detMap[0])
			case chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL:
				return fmt.Sprintf("Update the MS label to use the pool property value for %s", detMap[0])
			}
		case contObj:
			switch class {
			case chkpb.CheckInconsistClass_CIC_CONT_BAD_LABEL:
				return fmt.Sprintf("Reset the container property using the PS label for %s", detMap[1])
			}
		}
		return fmt.Sprintf("Trust the PS %s entry", ro)
	case chkpb.CheckInconsistAction_CIA_TRUST_TARGET:
		switch ro {
		case contObj:
			switch class {
			case chkpb.CheckInconsistClass_CIC_CONT_BAD_LABEL:
				return fmt.Sprintf("Update the CS label to use the container property value for %s", detMap[1])
			}
		}
		return fmt.Sprintf("Trust the %s result", ro)
	case chkpb.CheckInconsistAction_CIA_TRUST_MAJORITY:
		return fmt.Sprintf("Trust the majority of the %s results", ro)
	case chkpb.CheckInconsistAction_CIA_TRUST_LATEST:
		return fmt.Sprintf("Trust the latest %s result", ro)
	case chkpb.CheckInconsistAction_CIA_TRUST_OLDEST:
		return fmt.Sprintf("Trust the oldest %s result", ro)
	case chkpb.CheckInconsistAction_CIA_TRUST_EC_PARITY:
		return fmt.Sprintf("Trust the parity of the %s results", ro)
	case chkpb.CheckInconsistAction_CIA_TRUST_EC_DATA:
		return fmt.Sprintf("Trust the data of the %s results", ro)
	default:
		return fmt.Sprintf("%s: %s (details: %+v)", ro, action, details)
	}
}

// Trim leading/trailing whitespace from all string fields in the
// checker report.
func trimProtoSpaces(pm proto.Message) {
	pr := pm.ProtoReflect()
	pr.Range(func(fd protoreflect.FieldDescriptor, v protoreflect.Value) bool {
		if fd.Kind() == protoreflect.StringKind {
			if fd.IsList() {
				for i := 0; i < v.List().Len(); i++ {
					v.List().Set(i, protoreflect.ValueOf(strings.TrimSpace(v.List().Get(i).String())))
				}
			} else {
				pr.Set(fd, protoreflect.ValueOf(strings.TrimSpace(v.String())))
			}
		}
		return true
	})
}

func AnnotateFinding(f *Finding) *Finding {
	if f == nil {
		return nil
	}

	trimProtoSpaces(f)

	// Pad out the list of details as necessary to match
	// the length of the action list.
	if len(f.ActChoices) > 0 && len(f.ActDetails) != len(f.ActChoices) {
		for i := len(f.ActDetails); i < len(f.ActChoices); i++ {
			f.ActDetails = append(f.ActDetails, "")
		}
	}

	// If the report does not specify a list of informative messages to
	// accompany the list of actions, then create one.
	if len(f.ActMsgs) == 0 {
		if len(f.ActChoices) > 0 {
			f.ActMsgs = make([]string, len(f.ActChoices))
			for i, act := range f.ActChoices {
				f.ActMsgs[i] = descAction(f.Class, act, append([]string{f.PoolUuid, f.ContUuid}, f.ActDetails...)...)
			}
		} else {
			f.ActMsgs = make([]string, 1)
			f.ActMsgs[0] = descAction(f.Class, f.Action, append([]string{f.PoolUuid, f.ContUuid}, f.ActDetails...)...)
		}
	}
	if len(f.Msg) == 0 {
		f.Msg = fmt.Sprintf("Inconsistency found: %s (details: %+v)", f.Class, f.ActDetails)
	}

	return f
}
