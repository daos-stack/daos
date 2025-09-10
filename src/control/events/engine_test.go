//
// (C) Copyright 2020-2021 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package events

import (
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/test"
)

const (
	tHost        = "foo"
	tInstanceIdx = uint32(1)
	tRank        = uint32(1)
	tPid         = 1234
	tFmtType     = "Metadata"
	tIncarnation = uint64(456)
	tReason      = "test reason"
)

var (
	tExitErr = common.ExitStatus("test")
)

func mockEvtDied(t *testing.T) *RASEvent {
	t.Helper()
	return NewEngineDiedEvent(tHost, tInstanceIdx, tRank, tIncarnation, tExitErr, tPid)
}

func mockEvtFmtReq(t *testing.T) *RASEvent {
	t.Helper()
	return NewEngineFormatRequiredEvent(tHost, tInstanceIdx, tFmtType)
}

func TestEvents_NewEngineDiedEvent(t *testing.T) {
	evt := NewEngineDiedEvent(tHost, tInstanceIdx, tRank, tIncarnation, tExitErr, tPid)

	test.AssertEqual(t, RASEngineDied, evt.ID, "")
	test.AssertEqual(t, RASTypeStateChange, evt.Type, "")
	test.AssertEqual(t, RASSeverityError, evt.Severity, "")

	test.AssertEqual(t, "DAOS engine 1 exited unexpectedly: test", evt.Msg, "")

	test.AssertEqual(t, tHost, evt.Hostname, "")
	test.AssertEqual(t, tRank, evt.Rank, "")
	test.AssertEqual(t, tIncarnation, evt.Incarnation, "")
	test.AssertEqual(t, tPid, evt.ProcID, "")

	extInfo, ok := evt.ExtendedInfo.(*EngineStateInfo)
	if !ok {
		t.Fatalf("extended info is wrong type %t", evt.ExtendedInfo)
	}

	test.AssertEqual(t, tInstanceIdx, extInfo.InstanceIdx, "")
	test.CmpErr(t, tExitErr, extInfo.ExitErr)
}

func TestEvents_NewEngineJoinFailedEvent(t *testing.T) {
	evt := NewEngineJoinFailedEvent(tHost, tInstanceIdx, tRank, tIncarnation, tReason)

	test.AssertEqual(t, RASEngineJoinFailed, evt.ID, "")
	test.AssertEqual(t, RASTypeInfoOnly, evt.Type, "")
	test.AssertEqual(t, RASSeverityError, evt.Severity, "")

	test.AssertEqual(t, "DAOS engine 1 (rank 1) was not allowed to join the system", evt.Msg, "")

	test.AssertEqual(t, tHost, evt.Hostname, "")
	test.AssertEqual(t, tRank, evt.Rank, "")
	test.AssertEqual(t, tIncarnation, evt.Incarnation, "")

	extInfo, ok := evt.ExtendedInfo.(*StrInfo)
	if !ok {
		t.Fatalf("extended info is wrong type %t", evt.ExtendedInfo)
	}

	test.AssertEqual(t, tReason, string(*extInfo), "")
}

func TestEvents_ConvertEngineDied(t *testing.T) {
	event := mockEvtDied(t)

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
	event := mockEvtFmtReq(t)

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
