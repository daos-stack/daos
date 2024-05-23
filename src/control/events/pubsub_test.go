//
// (C) Copyright 2020-2023 Intel Corporation.
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

	ps := NewPubSub(test.Context(t), log)
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

	ps := NewPubSub(test.Context(t), log)

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

	ctx, cancel := context.WithTimeout(test.Context(t), 50*time.Millisecond)
	defer cancel()

	ps := NewPubSub(test.Context(t), log)
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

	ps := NewPubSub(test.Context(t), log)
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

	ps := NewPubSub(test.Context(t), log)
	defer ps.Close()

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
	test := func(t *testing.T, iter int) bool {
		log, buf := logging.NewTestLogger(t.Name())
		defer test.ShowBufferOnFailure(t, buf)

		ps := NewPubSub(test.Context(t), log)
		defer ps.Close()

		debounceType := RASSwimRankDead
		debounceCooldown := 25 * time.Millisecond
		tally := newTally(3)

		ps.Subscribe(RASTypeStateChange, tally)
		ps.Debounce(debounceType, debounceCooldown, func(ev *RASEvent) string {
			// Debounce should match all events sent in test.
			return fmt.Sprintf("%d", ev.Rank)
		})

		// Send 16 (var j) events per incarnation (var i). We should only see this event
		// three times as a new event will only be registered after the cooldown timer
		// expires at the end of each incarnation loop iteration.
		for i := 0; i < 3; i++ {
			log.Debugf("start loop %d", i+1)

			e := mockSwimRankDeadEvt(1, uint32(i+1))
			t := time.Now()
			for j := 0; j < 16; j++ {
				l := time.Since(t)
				if l >= debounceCooldown {
					// Test loop was stalled, print warning
					log.Noticef("test loop stalled for %s, restart iteration", l)
					return true
				}
				t = time.Now()
				ps.Publish(e)
			}

			log.Debugf("sleep for cooldown")
			time.Sleep(debounceCooldown)
		}

		<-tally.finished

		log.Debugf("test iteration %d", iter)

		// Expect one message from each incarnation loop, other repeated messages for the
		// same incarnation should get ignored by debounce Logic.
		test.AssertStringsEqual(t, []string{
			mockSwimRankDeadEvt(1, 1).String(),
			mockSwimRankDeadEvt(1, 2).String(),
			mockSwimRankDeadEvt(1, 3).String(),
		}, tally.getRx(), "unexpected slice of received events")

		return false
	}

	for tn := 0; tn < 1; tn++ {
		if restart := test(t, tn); restart {
			tn--
		}
	}
}
