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

func mockGenericEvent() *RASEvent {
	evt, _ := NewGenericEvent(
		math.MaxInt32-1, RASTypeInfoOnly, RASSeverityError,
		"DAOS generic test event", 1, "", "", "", "", "", "",
		"{\"people\":[\"bill\",\"steve\",\"bob\"]}")
	return evt
}

func TestEvents_ConvertGeneric(t *testing.T) {
	event := mockGenericEvent()

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
