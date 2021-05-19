//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package events

import (
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
)

const (
	tHost        = "foo"
	tInstanceIdx = 1
	tRank        = 1
	tPid         = 1234
	tFmtType     = "Metadata"
)

var (
	tExitErr = common.ExitStatus("test")
)

func mockDiedEvt(t *testing.T) *RASEvent {
	t.Helper()
	return NewEngineDiedEvent(tHost, tInstanceIdx, tRank, tExitErr, tPid)
}

func mockFmtReqEvt(t *testing.T) *RASEvent {
	t.Helper()
	return NewEngineFormatRequiredEvent(tHost, tInstanceIdx, tFmtType)
}

func TestEvents_ConvertEngineDied(t *testing.T) {
	event := mockDiedEvt(t)

	pbEvent, err := event.ToProto()
	if err != nil {
		t.Fatal(err)
	}

	t.Logf("proto event: %+v (%T)", pbEvent, pbEvent)

	returnedEvent := new(RASEvent)
	if err := returnedEvent.FromProto(pbEvent); err != nil {
		t.Fatal(err)
	}

	t.Logf("native event: %+v, %+v", returnedEvent, returnedEvent.ExtendedInfo)

	if diff := cmp.Diff(event, returnedEvent, defEvtCmpOpts...); diff != "" {
		t.Fatalf("unexpected event (-want, +got):\n%s\n", diff)
	}
}

func TestEvents_ConvertEngineFormatRequired(t *testing.T) {
	event := mockFmtReqEvt(t)

	pbEvent, err := event.ToProto()
	if err != nil {
		t.Fatal(err)
	}

	t.Logf("proto event: %+v (%T)", pbEvent, pbEvent)

	returnedEvent := new(RASEvent)
	if err := returnedEvent.FromProto(pbEvent); err != nil {
		t.Fatal(err)
	}

	t.Logf("native event: %+v, %+v", returnedEvent, returnedEvent.ExtendedInfo)

	if diff := cmp.Diff(event, returnedEvent, defEvtCmpOpts...); diff != "" {
		t.Fatalf("unexpected event (-want, +got):\n%s\n", diff)
	}
}
