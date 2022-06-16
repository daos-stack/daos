//
// (C) Copyright 2020-2022 Intel Corporation.
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
	// periodically check for debounced events that can be cleaned
	defaultDebounceCleanInterval = 1 * time.Hour
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

type (
	// DebounceKeyFn defines a function that returns a key to be used
	// for determining if the event matches a previously-seen event.
	DebounceKeyFn func(*RASEvent) string

	// dbncCtrlMsg contains details to be stored in the dbncCtrl map.
	dbncCtrlMsg struct {
		id       RASID
		keyFn    DebounceKeyFn
		cooldown time.Duration
	}

	// dbncCtrl stores debounce control messages for event IDs.
	dbncCtrl map[RASID]*dbncCtrlMsg
	// dbncEvts is used to keep track of events that have been seen
	// and their last seen time.
	dbncEvts map[RASID]map[string]time.Time
)

// updateLastSeen updates the last seen time for the given event ID and key.
func (de dbncEvts) updateLastSeen(id RASID, key string) {
	if _, found := de[id]; !found {
		de[id] = map[string]time.Time{}
	}
	de[id][key] = time.Now()
}

// PubSub stores subscriptions to event topics and handlers to be called on
// receipt of events pertaining to a particular topic.
type PubSub struct {
	log               logging.Logger
	events            chan *RASEvent
	subscribers       chan *subscriber
	handlers          map[RASTypeID][]Handler
	filterUpdates     chan *filterUpdate
	dbncCtrl          dbncCtrl
	dbncCtrlMsgs      chan *dbncCtrlMsg
	dbncEvts          dbncEvts
	dbncCleanInterval time.Duration
	disabledIDs       map[RASID]struct{}
	reset             chan struct{}
	shutdown          context.CancelFunc
}

// NewPubSub returns a reference to a newly initialized PubSub struct.
func NewPubSub(parent context.Context, log logging.Logger) *PubSub {
	ps := &PubSub{
		log:               log,
		events:            make(chan *RASEvent),
		subscribers:       make(chan *subscriber),
		handlers:          make(map[RASTypeID][]Handler),
		filterUpdates:     make(chan *filterUpdate),
		dbncCtrl:          make(dbncCtrl),
		dbncCtrlMsgs:      make(chan *dbncCtrlMsg),
		dbncEvts:          make(dbncEvts),
		dbncCleanInterval: defaultDebounceCleanInterval,
		disabledIDs:       make(map[RASID]struct{}),
		reset:             make(chan struct{}),
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

// Debounce accepts an event ID and a key function to be used to determine
// if an event matches a previously-seen event with that ID. This mechanism
// provides control over publication of duplicate events. The cooldown parameter
// determines the minimum amount of time required between publication of
// duplicate events. If cooldown is zero, then a given event may only
// be published at most once.
func (ps *PubSub) Debounce(id RASID, cooldown time.Duration, keyFn DebounceKeyFn) {
	select {
	case <-time.After(submitTimeout):
		ps.log.Errorf("failed to submit debounce update within %s", submitTimeout)
	case ps.dbncCtrlMsgs <- &dbncCtrlMsg{id: id, keyFn: keyFn, cooldown: cooldown}:
	}
}

func (ps *PubSub) debounceEvent(event *RASEvent) bool {
	ctrl, isControlled := ps.dbncCtrl[event.ID]
	if !isControlled {
		return false
	}

	key := ctrl.keyFn(event)
	if lastSeen, found := ps.dbncEvts[event.ID][key]; found {
		if ctrl.cooldown == 0 || time.Since(lastSeen) < ctrl.cooldown {
			ps.dbncEvts.updateLastSeen(event.ID, key)
			return true
		}
	}

	ps.dbncEvts.updateLastSeen(event.ID, key)
	return false
}

func (ps *PubSub) cleanDebouncedEvents() {
	for id, events := range ps.dbncEvts {
		ctrl, found := ps.dbncCtrl[id]
		if !found {
			delete(ps.dbncEvts, id)
			continue
		}

		for key, lastSeen := range events {
			if time.Since(lastSeen) > ps.dbncCleanInterval+ctrl.cooldown {
				delete(events, key)
			}
		}
	}
}

func (ps *PubSub) publish(ctx context.Context, event *RASEvent) {
	if _, exists := ps.disabledIDs[event.ID]; exists {
		return
	}

	if ps.debounceEvent(event) {
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
	cleanDebounceTicker := time.NewTicker(ps.dbncCleanInterval)

	for {
		select {
		case <-ctx.Done():
			ps.log.Debug("stopping event loop")
			cleanDebounceTicker.Stop()
			return
		case <-ps.reset:
			ps.handlers = make(map[RASTypeID][]Handler)
			ps.dbncCtrl = make(dbncCtrl)
			ps.dbncEvts = make(dbncEvts)
		case newSub := <-ps.subscribers:
			ps.handlers[newSub.topic] = append(ps.handlers[newSub.topic],
				newSub.handler)
		case event := <-ps.events:
			ps.publish(ctx, event)
		case fu := <-ps.filterUpdates:
			ps.updateFilter(fu)
		case msg := <-ps.dbncCtrlMsgs:
			ps.dbncCtrl[msg.id] = msg
		case <-cleanDebounceTicker.C:
			ps.cleanDebouncedEvents()
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
