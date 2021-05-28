//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package events

import (
	"math"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func mockEvtGeneric(t *testing.T) *RASEvent {
	t.Helper()
	return NewGenericEvent(RASID(math.MaxInt32-1), RASSeverityError,
		"DAOS generic test event", `{"people":["bill","steve","bob"]}`)
}

func TestEvents_ConvertGeneric(t *testing.T) {
	event := mockEvtGeneric(t)

	pbEvent, err := event.ToProto()
	if err != nil {
		t.Fatal(err)
	}

	t.Logf("proto event: %+v (%T)", pbEvent, pbEvent)

	returnedEvent := new(RASEvent)
	if err := returnedEvent.FromProto(pbEvent); err != nil {
		t.Fatal(err)
	}

	t.Logf("native event: %+v, %s", returnedEvent, *returnedEvent.GetStrInfo())

	if diff := cmp.Diff(event, returnedEvent, defEvtCmpOpts...); diff != "" {
		t.Fatalf("unexpected event (-want, +got):\n%s\n", diff)
	}
}
