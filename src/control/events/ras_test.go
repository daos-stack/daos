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
	"context"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/logging"
)

var defEvtCmpOpts = []cmp.Option{cmpopts.IgnoreUnexported(RASEvent{})}

func TestEvents_HandleClusterEvent(t *testing.T) {
	genericEvent := mockGenericEvent()
	pbGenericEvent, _ := genericEvent.ToProto()
	rankDownEvent := NewRankDownEvent("foo", 1, 1, common.ExitStatus("test"))
	pbRankDownEvent, _ := rankDownEvent.ToProto()
	psrEvent := NewPoolSvcReplicasUpdateEvent("foo", 1, common.MockUUID(), []uint32{0, 1}, 1)
	pbPSREvent, _ := psrEvent.ToProto()

	for name, tc := range map[string]struct {
		req         *sharedpb.ClusterEventReq
		subType     RASTypeID
		expEvtTypes []string
		expResp     *sharedpb.ClusterEventResp
		expErr      error
	}{
		"nil req": {
			expErr: errors.New("nil request"),
		},
		"nil event": {
			req:    &sharedpb.ClusterEventReq{},
			expErr: errors.New("nil event in request"),
		},
		"generic event": {
			req: &sharedpb.ClusterEventReq{
				Event: pbGenericEvent,
			},
			subType:     RASTypeInfoOnly,
			expEvtTypes: []string{RASTypeInfoOnly.String()},
			expResp:     &sharedpb.ClusterEventResp{},
		},
		"filtered generic event": {
			req: &sharedpb.ClusterEventReq{
				Event: pbGenericEvent,
			},
			subType: RASTypeStateChange,
			expResp: &sharedpb.ClusterEventResp{},
		},
		"rank down event": {
			req: &sharedpb.ClusterEventReq{
				Event: pbRankDownEvent,
			},
			subType:     RASTypeStateChange,
			expEvtTypes: []string{RASTypeStateChange.String()},
			expResp:     &sharedpb.ClusterEventResp{},
		},
		"filtered rank down event": {
			req: &sharedpb.ClusterEventReq{
				Event: pbRankDownEvent,
			},
			subType: RASTypeInfoOnly,
			expResp: &sharedpb.ClusterEventResp{},
		},
		"pool svc replica update event": {
			req: &sharedpb.ClusterEventReq{
				Event: pbPSREvent,
			},
			subType:     RASTypeStateChange,
			expEvtTypes: []string{RASTypeStateChange.String()},
			expResp:     &sharedpb.ClusterEventResp{},
		},
		"filtered pool svc replica update event": {
			req: &sharedpb.ClusterEventReq{
				Event: pbPSREvent,
			},
			subType: RASTypeInfoOnly,
			expResp: &sharedpb.ClusterEventResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ctx := context.Background()

			ps := NewPubSub(ctx, log)
			defer ps.Close()

			tly1 := newTally(len(tc.expEvtTypes))

			ps.Subscribe(tc.subType, tly1)

			resp, err := ps.HandleClusterEvent(tc.req)
			common.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			select {
			case <-time.After(50 * time.Millisecond):
			case <-tly1.finished:
			}

			common.AssertStringsEqual(t, tc.expEvtTypes, tly1.getRx(),
				"unexpected received events")

			if diff := cmp.Diff(tc.expResp, resp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
