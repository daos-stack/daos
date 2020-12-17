//
// (C) Copyright 2019-2020 Intel Corporation.
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

	"github.com/daos-stack/daos/src/control/logging"
)

// Handler any object that implements the Handler interface can be registered to
// receive events.
type Handler interface {
	// OnEvent will receive an event to be processed and a context for
	// cancellation. Implementation must return when context is done.
	OnEvent(context.Context, Event)
}

type subscriber struct {
	topic   RASTypeID
	handler Handler
}

// PubSub stores subscriptions to event topics and handlers to be called on
// receipt of events pertaining to a particular topic.
type PubSub struct {
	log         logging.Logger
	events      chan Event
	eventMask   uint32
	subscribers chan *subscriber
	handlers    map[RASTypeID][]Handler
	reset       chan struct{}
	shutdown    context.CancelFunc
}

// DisableEventIDs adds event IDs to the filter mask preventing those event IDs from
// being published.
func (ps *PubSub) DisableEventIDs(ids ...RASID) {
	for _, id := range ids {
		ps.eventMask = ps.eventMask | uint32(id)
	}
}

// EnableEventIDs removes event IDs from the filter mask enabling those event IDs
// to be published.
func (ps *PubSub) EnableEventIDs(ids ...RASID) {
	for _, id := range ids {
		ps.eventMask = ps.eventMask &^ uint32(id)
	}
}

// Publish passes an event to the event channel to be processed by subscribers.
// Ignore masked events.
func (ps *PubSub) Publish(event Event) {
	id := uint32(event.GetID())
	if id&ps.eventMask != 0 {
		ps.log.Debugf("event %s ignored by filter", event.GetID())
		return
	}

	topic := event.GetType()
	ps.log.Debugf("publishing @%s: %+v", topic, event)

	ps.events <- event
}

// Subscribe adds a handler to the list of handlers subscribed to a given topic
// (event type).
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
			for _, hdlr := range ps.handlers[event.GetType()] {
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

// NewPubSub returns a reference to a newly initialized PubSub struct.
func NewPubSub(parent context.Context, log logging.Logger) *PubSub {
	ps := &PubSub{
		log:         log,
		reset:       make(chan struct{}),
		subscribers: make(chan *subscriber),
		events:      make(chan Event),
		handlers:    make(map[RASTypeID][]Handler),
	}

	ctx, cancel := context.WithCancel(parent)
	ps.shutdown = cancel
	go ps.eventLoop(ctx)

	return ps
}
