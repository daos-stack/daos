//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package events

import (
	"context"
	"time"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

const (
	// set a timeout to prevent a deadlock on channel write
	submitTimeout = 1 * time.Second
)

// Handler defines an interface to be implemented by event receivers.
type Handler interface {
	// OnEvent takes an event to be processed and a context,
	// implementation must return on context.Done() and be thread safe.
	OnEvent(context.Context, *RASEvent)
}

// HandlerFunc is an adapter to allow an ordinary function to be
// used as a Handler.
type HandlerFunc func(context.Context, *RASEvent)

// OnEvent implements the Handler interface.
func (f HandlerFunc) OnEvent(ctx context.Context, evt *RASEvent) {
	f(ctx, evt)
}

// Publisher defines an interface to be implemented by event publishers.
type Publisher interface {
	// Publish takes an event to be published.
	Publish(event *RASEvent)
}

type subscriber struct {
	topic   RASTypeID
	handler Handler
}

// filterUpdate enables or disables publishing of given event ids.
type filterUpdate struct {
	enable bool
	ids    []RASID
}

// PubSub stores subscriptions to event topics and handlers to be called on
// receipt of events pertaining to a particular topic.
type PubSub struct {
	log           logging.Logger
	events        chan *RASEvent
	subscribers   chan *subscriber
	handlers      map[RASTypeID][]Handler
	filterUpdates chan *filterUpdate
	disabledIDs   map[RASID]struct{}
	reset         chan struct{}
	shutdown      context.CancelFunc
}

// NewPubSub returns a reference to a newly initialized PubSub struct.
func NewPubSub(parent context.Context, log logging.Logger) *PubSub {
	ps := &PubSub{
		log:           log,
		events:        make(chan *RASEvent),
		subscribers:   make(chan *subscriber),
		handlers:      make(map[RASTypeID][]Handler),
		filterUpdates: make(chan *filterUpdate),
		disabledIDs:   make(map[RASID]struct{}),
		reset:         make(chan struct{}),
	}

	ctx, cancel := context.WithCancel(parent)
	ps.shutdown = cancel
	go ps.eventLoop(ctx)

	return ps
}

// DisableEventIDs adds event IDs to the filter preventing those event IDs from
// being published.
func (ps *PubSub) DisableEventIDs(ids ...RASID) {
	select {
	case <-time.After(submitTimeout):
		ps.log.Errorf("failed to submit filter update within %s", submitTimeout)
	case ps.filterUpdates <- &filterUpdate{enable: false, ids: ids}:
	}
}

// EnableEventIDs removes event IDs from the filter enabling those event IDs
// to be published.
func (ps *PubSub) EnableEventIDs(ids ...RASID) {
	select {
	case <-time.After(submitTimeout):
		ps.log.Errorf("failed to submit filter update within %s", submitTimeout)
	case ps.filterUpdates <- &filterUpdate{enable: true, ids: ids}:
	}
}

// Publish passes an event to the event channel to be processed by subscribers.
// Ignore disabled events.
func (ps *PubSub) Publish(event *RASEvent) {
	if common.InterfaceIsNil(event) {
		ps.log.Error("nil event")
		return
	}
	select {
	case <-time.After(submitTimeout):
		ps.log.Errorf("failed to submit event within %s", submitTimeout)
	case ps.events <- event:
	}
}

// Subscribe adds a handler to the list of handlers subscribed to a given topic
// (event type).
//
// The special case "RASTypeAny" topic will handle all received events.
func (ps *PubSub) Subscribe(topic RASTypeID, handler Handler) {
	select {
	case <-time.After(submitTimeout):
		ps.log.Errorf("failed to submit subscription within %s", submitTimeout)
	case ps.subscribers <- &subscriber{
		topic:   topic,
		handler: handler,
	}:
	}
}

func (ps *PubSub) publish(ctx context.Context, event *RASEvent) {
	if _, exists := ps.disabledIDs[event.ID]; exists {
		return
	}

	for _, hdlr := range ps.handlers[RASTypeAny] {
		go hdlr.OnEvent(ctx, event)
	}
	for _, hdlr := range ps.handlers[event.Type] {
		go hdlr.OnEvent(ctx, event)
	}
}

func (ps *PubSub) updateFilter(fu *filterUpdate) {
	for _, id := range fu.ids {
		_, exists := ps.disabledIDs[id]
		switch {
		case exists && fu.enable:
			delete(ps.disabledIDs, id)
		case !exists && !fu.enable:
			ps.disabledIDs[id] = struct{}{}
		}
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
			ps.publish(ctx, event)
		case fu := <-ps.filterUpdates:
			ps.updateFilter(fu)
		}
	}
}

// Close terminates event loop by calling shutdown to cancel context.
func (ps *PubSub) Close() {
	ps.shutdown()
}

// Reset clears registered handlers by sending reset.
func (ps *PubSub) Reset() {
	ps.reset <- struct{}{}
}
