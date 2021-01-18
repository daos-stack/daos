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
	"context"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/logging"
)

// Handler defines an interface to be implemented by event receivers.
type Handler interface {
	// OnEvent takes an event to be processed and a context,
	// implementation must return on context.Done().
	OnEvent(context.Context, *RASEvent)
}

type subscriber struct {
	topic   RASTypeID
	handler Handler
}

// PubSub stores subscriptions to event topics and handlers to be called on
// receipt of events pertaining to a particular topic.
type PubSub struct {
	log         logging.Logger
	events      chan *RASEvent
	subscribers chan *subscriber
	handlers    map[RASTypeID][]Handler
	disabledIDs map[RASID]struct{}
	reset       chan struct{}
	shutdown    context.CancelFunc
}

// NewPubSub returns a reference to a newly initialized PubSub struct.
func NewPubSub(parent context.Context, log logging.Logger) *PubSub {
	ps := &PubSub{
		log:         log,
		events:      make(chan *RASEvent),
		subscribers: make(chan *subscriber),
		handlers:    make(map[RASTypeID][]Handler),
		disabledIDs: make(map[RASID]struct{}),
		reset:       make(chan struct{}),
	}

	ctx, cancel := context.WithCancel(parent)
	ps.shutdown = cancel
	go ps.eventLoop(ctx)

	return ps
}

// DisableEventIDs adds event IDs to the filter preventing those event IDs from
// being published.
func (ps *PubSub) DisableEventIDs(ids ...RASID) {
	for _, id := range ids {
		ps.disabledIDs[id] = struct{}{}
	}
}

// EnableEventIDs removes event IDs from the filter enabling those event IDs
// to be published.
func (ps *PubSub) EnableEventIDs(ids ...RASID) {
	for _, id := range ids {
		if _, exists := ps.disabledIDs[id]; exists {
			delete(ps.disabledIDs, id)
		}
	}
}

// Publish passes an event to the event channel to be processed by subscribers.
// Ignore disabled events.
func (ps *PubSub) Publish(event *RASEvent) {
	if common.InterfaceIsNil(event) {
		ps.log.Error("nil event")
		return
	}

	if _, exists := ps.disabledIDs[event.ID]; exists {
		ps.log.Debugf("event %s ignored by filter", event.ID)
		return
	}

	ps.log.Debugf("publishing @%s: %s", event.Type, event.ID)

	ps.events <- event
}

// Subscribe adds a handler to the list of handlers subscribed to a given topic
// (event type).
//
// The special case "RASTypeAny" topic will handle all received events.
func (ps *PubSub) Subscribe(topic RASTypeID, handler Handler) {
	ps.subscribers <- &subscriber{
		topic:   topic,
		handler: handler,
	}
}

// eventLoop takes a lockless approach trading a little performance for
// simplicity. Select on one of cancellation/reset/additional subscriber/new
// event.
func (ps *PubSub) eventLoop(ctx context.Context) {
	for {
		select {
		case <-ctx.Done():
			ps.log.Debug("stopping event loop")
			return
		case <-ps.reset:
			ps.handlers = make(map[RASTypeID][]Handler)
		case newSub := <-ps.subscribers:
			ps.handlers[newSub.topic] = append(ps.handlers[newSub.topic],
				newSub.handler)
		case event := <-ps.events:
			for _, hdlr := range ps.handlers[RASTypeAny] {
				go hdlr.OnEvent(ctx, event)
			}
			for _, hdlr := range ps.handlers[event.Type] {
				go hdlr.OnEvent(ctx, event)
			}
		}
	}
}

// Close terminates event loop by calling shutdown to cancel context.
func (ps *PubSub) Close() {
	ps.log.Debug("called Close()")
	ps.shutdown()
}

// Reset clears registered handlers by sending reset.
func (ps *PubSub) Reset() {
	ps.log.Debug("called Reset()")
	ps.reset <- struct{}{}
}

// HandleClusterEvent extracts event field from protobuf request message and
// converts to concrete event type that implements the Event interface.
// The Event is then published to make available to locally subscribed consumers
// to act upon.
func (ps *PubSub) HandleClusterEvent(req *sharedpb.ClusterEventReq) (*sharedpb.ClusterEventResp, error) {
	switch {
	case req.Sequence < 0:
		ps.log.Debug("no sequence number in ClusterEventReq")
	case req == nil:
		return nil, errors.New("nil ClusterEventReq")
	case req.Event == nil:
		return nil, errors.New("nil Event in ClusterEventReq")
	}

	event, err := NewFromProto(req.Event)
	if err != nil {
		return nil, err
	}
	ps.Publish(event)

	return &sharedpb.ClusterEventResp{Sequence: req.Sequence}, nil
}
