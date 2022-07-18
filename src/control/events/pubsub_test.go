//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package events

import (
	"context"
	"fmt"
	"sync"
	"testing"
	"time"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

func newTally(expCount int) *tally {
	return &tally{
		expectedRx: expCount,
		finished:   make(chan struct{}),
	}
}

type tally struct {
	sync.Mutex
	finished   chan struct{}
	expectedRx int
	rx         []string
}

func (tly *tally) OnEvent(_ context.Context, evt *RASEvent) {
	tly.Lock()
	defer tly.Unlock()

	tly.rx = append(tly.rx, evt.String())
	if len(tly.rx) == tly.expectedRx {
		close(tly.finished)
	}
}

func (tly *tally) getRx() []string {
	tly.Lock()
	defer tly.Unlock()

	return tly.rx
}

func TestEvents_PubSub_Basic(t *testing.T) {
	evt1 := mockEvtDied(t)

	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	ctx := context.Background()

	ps := NewPubSub(ctx, log)
	defer ps.Close()

	tly1 := newTally(2)
	tly2 := newTally(2)

	ps.Subscribe(RASTypeStateChange, tly1)
	ps.Subscribe(RASTypeStateChange, tly2)

	ps.Publish(evt1)
	ps.Publish(evt1)

	<-tly1.finished
	<-tly2.finished

	test.AssertStringsEqual(t, []string{evt1.String(), evt1.String()},
		tly1.getRx(), "tly1 unexpected slice of received events")
	test.AssertStringsEqual(t, []string{evt1.String(), evt1.String()},
		tly2.getRx(), "tly2 unexpected slice of received events")
}

func TestEvents_PubSub_Reset(t *testing.T) {
	evt1 := mockEvtDied(t)

	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	tly1 := newTally(2)
	tly2 := newTally(2)

	ctx := context.Background()

	ps := NewPubSub(ctx, log)

	ps.Subscribe(RASTypeStateChange, tly1)

	ps.Publish(evt1)
	ps.Publish(evt1)

	<-tly1.finished

	ps.Reset()

	test.AssertStringsEqual(t, []string{evt1.String(), evt1.String()},
		tly1.getRx(), "unexpected slice of received events")
	test.AssertEqual(t, 0, len(tly2.getRx()), "unexpected number of received events")

	tly1 = newTally(2)
	tly2 = newTally(2)

	ps.Subscribe(RASTypeStateChange, tly2)

	ps.Publish(evt1)
	ps.Publish(evt1)

	<-tly2.finished
	ps.Close()

	test.AssertStringsEqual(t, []string{evt1.String(), evt1.String()},
		tly2.getRx(), "unexpected slice of received events")
	test.AssertEqual(t, 0, len(tly1.getRx()), "unexpected number of received events")
}

func TestEvents_PubSub_DisableEvent(t *testing.T) {
	evt1 := mockEvtDied(t)
	evt2 := mockEvtSvcReps(t)

	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	tly1 := newTally(2)

	ctx, cancel := context.WithTimeout(context.Background(), 50*time.Millisecond)
	defer cancel()

	ps := NewPubSub(context.Background(), log)
	defer ps.Close()

	ps.Subscribe(RASTypeStateChange, tly1)

	ps.DisableEventIDs(evt1.ID, evt2.ID)

	ps.Publish(evt1)
	ps.Publish(evt2)

	<-ctx.Done()
	test.AssertEqual(t, 0, len(tly1.getRx()), "unexpected number of received events")

	ps.EnableEventIDs(evt1.ID, evt2.ID)

	ps.Publish(evt1)
	ps.Publish(evt1)

	<-tly1.finished
	test.AssertEqual(t, 2, len(tly1.getRx()), "unexpected number of received events")
}

func TestEvents_PubSub_SubscribeAnyTopic(t *testing.T) {
	evt1 := mockEvtDied(t)
	evt2 := mockEvtGeneric(t) // of type InfoOnly will only match Any

	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	ctx := context.Background()

	ps := NewPubSub(ctx, log)
	defer ps.Close()

	tly1 := newTally(3)
	tly2 := newTally(2)

	ps.Subscribe(RASTypeAny, tly1)
	ps.Subscribe(RASTypeStateChange, tly2)

	ps.Publish(evt1)
	ps.Publish(evt1)
	ps.Publish(evt2)

	<-tly1.finished
	<-tly2.finished

	test.AssertStringsEqual(t, []string{evt1.String(), evt1.String(), evt2.String()},
		tly1.getRx(), "tly1 unexpected slice of received events")

	test.AssertStringsEqual(t, []string{evt1.String(), evt1.String()},
		tly2.getRx(), "tly2 unexpected slice of received events")
}

func mockSwimRankDeadEvt(rankInc ...uint32) *RASEvent {
	var rank uint32
	var inc uint64

	switch len(rankInc) {
	case 1:
		rank = rankInc[0]
	case 2:
		rank = rankInc[0]
		inc = uint64(rankInc[1])
	}
	return &RASEvent{
		ID:          RASSwimRankDead,
		Type:        RASTypeStateChange,
		Rank:        rank,
		Incarnation: inc,
	}
}

func TestEvents_PubSub_Debounce_NoCooldown(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	ps := NewPubSub(ctx, log)

	evt1 := mockSwimRankDeadEvt(1, 1)
	debounceType := evt1.ID
	evt1_2 := mockSwimRankDeadEvt(1, 2)
	evt2 := mockSwimRankDeadEvt(2, 2)
	evt3 := mockEvtDied(t)
	tally := newTally(4)

	ps.Subscribe(RASTypeStateChange, tally)
	ps.Debounce(debounceType, 0, func(ev *RASEvent) string {
		return fmt.Sprintf("%d:%x", ev.Rank, ev.Incarnation)
	})

	ps.Publish(evt1)
	ps.Publish(evt1_2)
	ps.Publish(evt2)
	ps.Publish(evt3)

	// we should only see one of these, no matter how many times
	// it's submitted
	for i := 0; i < 16; i++ {
		ps.Publish(evt1)
	}

	<-tally.finished

	test.AssertStringsEqual(t, []string{evt1.String(), evt1_2.String(), evt2.String(), evt3.String()},
		tally.getRx(), "unexpected slice of received events")
}

func TestEvents_PubSub_Debounce_Cooldown(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	ps := NewPubSub(ctx, log)

	evt1 := mockSwimRankDeadEvt(1, 1)
	debounceType := evt1.ID
	debounceCooldown := 10 * time.Millisecond
	tally := newTally(3)

	ps.Subscribe(RASTypeStateChange, tally)
	ps.Debounce(debounceType, debounceCooldown, func(ev *RASEvent) string {
		return fmt.Sprintf("%d:%x", ev.Rank, ev.Incarnation)
	})

	// We should only see this event three times, after the cooldown
	// timer expires on each loop.
	for i := 0; i < 3; i++ {
		for j := 0; j < 16; j++ {
			ps.Publish(evt1)
		}
		time.Sleep(debounceCooldown)
	}

	<-tally.finished

	test.AssertStringsEqual(t, []string{evt1.String(), evt1.String(), evt1.String()},
		tally.getRx(), "unexpected slice of received events")
}
