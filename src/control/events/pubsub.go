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
	"sync"

	"github.com/daos-stack/daos/src/control/logging"
)

// Handler any object that implements the Handler interface can be registered to
// receive events.
type Handler interface {
	// OnEvent will receive an event to be processed and a context for
	// cancellation. Implementation must return when context is done.
	OnEvent(context.Context, Event)
}

// PubSub stores subscriptions to event topics and handlers to be called on
// receipt of events pertaining to a particular topic.
type PubSub struct {
	sync.RWMutex
	log      logging.Logger
	streams  map[RASTypeID]chan Event
	handlers map[RASTypeID][]Handler
	closed   bool
}

// Publish passes an event to the stream (channel) dedicated to the event's
// topic (type).
func (ps *PubSub) Publish(event Event) {
	topic := event.GetType()
	ps.log.Debugf("publishing @%s: %+v", topic, event)

	ps.RLock()
	defer ps.RUnlock()

	if ps.closed {
		ps.log.Debugf("attempt to publish on closed pubsub")
		return
	}

	if len(ps.handlers[topic]) == 0 {
		ps.log.Debugf("no handlers registered to topic %s", topic)
		return
	}

	ps.streams[topic] <- event
}

func (ps *PubSub) startProcessing(ctx context.Context, topic RASTypeID) {
	ps.log.Debugf("start event processing loop for topic %s", topic)
	for evt := range ps.streams[topic] {
		ps.RLock()
		for idx, handler := range ps.handlers[topic] {
			ps.log.Debugf("calling event handler %d", idx)
			handler.OnEvent(ctx, evt)
		}
		ps.RUnlock()
	}
	ps.log.Debugf("finish event processing loop for topic %s", topic)
}

// Subscribe adds a handler function to the list of handlers subscribed to a
// given topic (event type), then begin processing events of the given topic
// if not already started.
//
// Context is supplied to provide cancellation of event processor goroutines
// started for each handler in startProcessing function.
//
// On Close(), each of the event stream channels will be closed and
// startProcessing will exit, there is an expectation that the supplied context
// should be cancelled (in the calling function) when the Close() or Reset()
// methods are called and vice versa.
func (ps *PubSub) Subscribe(ctx context.Context, topic RASTypeID, handler Handler) {
	ps.Lock()
	ps.log.Debugf("registering handler for topic %s", topic)
	if _, exists := ps.streams[topic]; !exists {
		ps.streams[topic] = make(chan Event, 1)
	}
	ps.handlers[topic] = append(ps.handlers[topic], handler)
	ps.Unlock()

	go ps.startProcessing(ctx, topic)
}

func (ps *PubSub) _close() {
	if !ps.closed {
		for _, ch := range ps.streams {
			close(ch)
		}
		ps.closed = true
	}
}

// Close terminates all streams by closing relevant channels which in turn
// finishes the event processing loops listening on event streams.
func (ps *PubSub) Close() {
	ps.log.Debug("closing")
	ps.Lock()
	defer ps.Unlock()

	ps._close()
}

// Reset clears and reinitializes streams and handlers.
func (ps *PubSub) Reset() {
	ps.log.Debug("resetting")
	ps.Lock()
	defer ps.Unlock()

	ps._close()
	ps.streams = make(map[RASTypeID]chan Event)
	ps.handlers = make(map[RASTypeID][]Handler)
	ps.closed = false
}

// NewPubSub returns a reference to a newly initialized PubSub struct.
func NewPubSub(log logging.Logger) *PubSub {
	return &PubSub{
		log:      log,
		streams:  make(map[RASTypeID]chan Event),
		handlers: make(map[RASTypeID][]Handler),
	}
}
