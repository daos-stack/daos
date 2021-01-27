//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package events

import (
	"context"
	"sync"
	"testing"
	"time"

	"github.com/daos-stack/daos/src/control/common"
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

	tly.rx = append(tly.rx, evt.Type.String())
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
	evt1 := NewRankDownEvent("foo", 1, 1, common.ExitStatus("test"))

	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

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

	common.AssertStringsEqual(t, []string{
		RASTypeStateChange.String(), RASTypeStateChange.String(),
	}, tly1.getRx(), "tly1 unexpected slice of received events")
	common.AssertStringsEqual(t, []string{
		RASTypeStateChange.String(), RASTypeStateChange.String(),
	}, tly2.getRx(), "tly2 unexpected slice of received events")
}

func TestEvents_PubSub_Reset(t *testing.T) {
	evt1 := NewRankDownEvent("foo", 1, 1, common.ExitStatus("test"))

	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	tly1 := newTally(2)
	tly2 := newTally(2)

	ctx := context.Background()

	ps := NewPubSub(ctx, log)

	ps.Subscribe(RASTypeStateChange, tly1)

	ps.Publish(evt1)
	ps.Publish(evt1)

	<-tly1.finished

	ps.Reset()

	common.AssertStringsEqual(t, []string{
		RASTypeStateChange.String(), RASTypeStateChange.String(),
	}, tly1.getRx(), "unexpected slice of received events")
	common.AssertEqual(t, 0, len(tly2.getRx()), "unexpected number of received events")

	tly1 = newTally(2)
	tly2 = newTally(2)

	ps.Subscribe(RASTypeStateChange, tly2)

	ps.Publish(evt1)
	ps.Publish(evt1)

	<-tly2.finished
	ps.Close()

	common.AssertStringsEqual(t, []string{
		RASTypeStateChange.String(), RASTypeStateChange.String(),
	}, tly2.getRx(), "unexpected slice of received events")
	common.AssertEqual(t, 0, len(tly1.getRx()), "unexpected number of received events")
}

func TestEvents_PubSub_DisableEvent(t *testing.T) {
	evt1 := NewRankDownEvent("foo", 1, 1, common.ExitStatus("test"))

	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	tly1 := newTally(2)

	ctx, cancel := context.WithTimeout(context.Background(), 50*time.Millisecond)
	defer cancel()

	ps := NewPubSub(context.Background(), log)
	defer ps.Close()

	ps.Subscribe(RASTypeStateChange, tly1)

	ps.DisableEventIDs(evt1.ID)

	ps.Publish(evt1)
	ps.Publish(evt1)

	<-ctx.Done()
	common.AssertEqual(t, 0, len(tly1.getRx()), "unexpected number of received events")

	ps.EnableEventIDs(evt1.ID)

	ps.Publish(evt1)
	ps.Publish(evt1)

	<-tly1.finished
	common.AssertEqual(t, 2, len(tly1.getRx()), "unexpected number of received events")
}

func TestEvents_PubSub_SubscribeAnyTopic(t *testing.T) {
	evt1 := NewRankDownEvent("foo", 1, 1, common.ExitStatus("test"))

	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	ctx := context.Background()

	ps := NewPubSub(ctx, log)
	defer ps.Close()

	tly1 := newTally(3)
	tly2 := newTally(2)

	ps.Subscribe(RASTypeAny, tly1)
	ps.Subscribe(RASTypeStateChange, tly2)

	ps.Publish(evt1)
	ps.Publish(evt1)
	ps.Publish(mockGenericEvent()) // of type InfoOnly will only match Any

	<-tly1.finished
	<-tly2.finished

	common.AssertStringsEqual(t, []string{
		RASTypeInfoOnly.String(),
		RASTypeStateChange.String(),
		RASTypeStateChange.String(),
	}, tly1.getRx(), "tly1 unexpected slice of received events")

	common.AssertStringsEqual(t, []string{
		RASTypeStateChange.String(),
		RASTypeStateChange.String(),
	}, tly2.getRx(), "tly2 unexpected slice of received events")
}
