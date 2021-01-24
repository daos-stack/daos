//
// (C) Copyright 2020-2021 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
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

	if diff := cmp.Diff(event, returnedEvent); diff != "" {
		t.Fatalf("unexpected event (-want, +got):\n%s\n", diff)
	}
}
