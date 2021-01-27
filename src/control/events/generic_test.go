//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package events

import (
	"math"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
)

func mockGenericEvent() *RASEvent {
	si := StrInfo("{\"people\":[\"bill\",\"steve\",\"bob\"]}")

	return &RASEvent{
		Timestamp:    common.FormatTime(time.Now()),
		Msg:          "DAOS generic test event",
		ID:           math.MaxInt32 - 1,
		Hostname:     "foo",
		Rank:         1,
		Type:         RASTypeInfoOnly,
		Severity:     RASSeverityError,
		ExtendedInfo: &si,
	}
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
