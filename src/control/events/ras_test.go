//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package events

import (
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

			ps := NewPubSub(test.Context(t), log)
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
