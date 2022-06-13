//
// (C) Copyright 2020-2022 Intel Corporation.
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

	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

var defEvtCmpOpts = append(test.DefaultCmpOpts(),
	cmpopts.IgnoreUnexported(RASEvent{}),
)

func TestEvents_HandleClusterEvent(t *testing.T) {
	genericEvent := mockEvtGeneric(t)
	pbGenericEvent, _ := genericEvent.ToProto()
	engineDiedEvent := mockEvtDied(t)
	pbEngineDiedEvent, _ := engineDiedEvent.ToProto()
	psrEvent := mockEvtSvcReps(t)
	pbPSREvent, _ := psrEvent.ToProto()

	for name, tc := range map[string]struct {
		req     *sharedpb.ClusterEventReq
		subType RASTypeID
		fwded   bool
		expEvts []string
		expResp *sharedpb.ClusterEventResp
		expErr  error
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
			subType: RASTypeInfoOnly,
			expEvts: []string{genericEvent.String()},
			expResp: &sharedpb.ClusterEventResp{},
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
				Event: pbEngineDiedEvent,
			},
			subType: RASTypeStateChange,
			expEvts: []string{engineDiedEvent.String()},
			expResp: &sharedpb.ClusterEventResp{},
		},
		"filtered rank down event": {
			req: &sharedpb.ClusterEventReq{
				Event: pbEngineDiedEvent,
			},
			subType: RASTypeInfoOnly,
			expResp: &sharedpb.ClusterEventResp{},
		},
		"pool svc replica update event": {
			req: &sharedpb.ClusterEventReq{
				Event: pbPSREvent,
			},
			subType: RASTypeStateChange,
			expEvts: []string{psrEvent.String()},
			expResp: &sharedpb.ClusterEventResp{},
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
			defer test.ShowBufferOnFailure(t, buf)

			ctx := context.Background()

			ps := NewPubSub(ctx, log)
			defer ps.Close()

			tly1 := newTally(len(tc.expEvts))

			ps.Subscribe(tc.subType, tly1)

			resp, err := ps.HandleClusterEvent(tc.req, tc.fwded)
			test.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			select {
			case <-time.After(50 * time.Millisecond):
			case <-tly1.finished:
			}

			test.AssertStringsEqual(t, tc.expEvts, tly1.getRx(),
				"unexpected received events")

			if diff := cmp.Diff(tc.expResp, resp, defEvtCmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
