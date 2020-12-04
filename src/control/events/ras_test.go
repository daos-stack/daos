//
// (C) Copyright 2020 Intel Corporation.
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
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

func TestEvents_ConvertRASEvent(t *testing.T) {
	rasEvent := &RASEvent{
		Name:      "some name",
		Timestamp: "some date",
		Msg:       "some message",
		Hostname:  "some hostname",
		Rank:      1,
		ID:        RASRankNoResp,
		Severity:  RASSeverityInfo,
		Type:      RASTypeInfoOnly,
	}

	pbRASEvent := new(mgmtpb.RASEvent)
	if err := convert.Types(rasEvent, pbRASEvent); err != nil {
		t.Fatal(err)
	}

	t.Logf("proto ras event: %+v", pbRASEvent)

	returnedRASEvent := new(RASEvent)
	if err := convert.Types(pbRASEvent, returnedRASEvent); err != nil {
		t.Fatal(err)
	}

	t.Logf("native ras event: %+v", returnedRASEvent)

	if diff := cmp.Diff(rasEvent, returnedRASEvent); diff != "" {
		t.Fatalf("unexpected event (-want, +got):\n%s\n", diff)
	}
}
