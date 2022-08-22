//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package events

import (
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common/test"
)

const (
	tLdrTerm = 1
)

var (
	tUuid    = test.MockUUID(1)
	tSvcReps = []uint32{0, 1}
)

func mockEvtSvcReps(t *testing.T) *RASEvent {
	t.Helper()
	return NewPoolSvcReplicasUpdateEvent(tHost, tRank, tUuid, tSvcReps, tLdrTerm)
}

func TestEvents_ConvertPoolSvcReplicasUpdate(t *testing.T) {
	event := mockEvtSvcReps(t)

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
