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
	"sync"
	"testing"
	"time"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

type tally struct {
	sync.Mutex
	rx []string
}

func (tly *tally) OnEvent(_ context.Context, evt Event) {
	tly.Lock()
	defer tly.Unlock()

	tly.rx = append(tly.rx, evt.GetType().String())
}

func (tly *tally) getRx() []string {
	tly.Lock()
	defer tly.Unlock()

	return tly.rx
}

func TestEvents_PubSub_Basic(t *testing.T) {
	evt1 := NewRankExitEvent("foo", 1, 1, common.ExitStatus("test"))

	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	ctx, cancel := context.WithTimeout(context.Background(), 500*time.Millisecond)
	defer cancel()

	ps := NewPubSub(log)
	defer ps.Close()

	tly1 := &tally{}
	tly2 := &tally{}

	ps.Subscribe(ctx, RASTypeRankStateChange, tly1)
	ps.Subscribe(ctx, RASTypeRankStateChange, tly2)

	ps.Publish(evt1)
	ps.Publish(evt1)

	<-ctx.Done()

	common.AssertStringsEqual(t, []string{
		RASTypeRankStateChange.String(), RASTypeRankStateChange.String(),
	}, tly1.getRx(), "unexpected slice of received events")
	common.AssertStringsEqual(t, []string{
		RASTypeRankStateChange.String(), RASTypeRankStateChange.String(),
	}, tly2.getRx(), "unexpected slice of received events")
}

func TestEvents_PubSub_Reset(t *testing.T) {
	evt1 := NewRankExitEvent("foo", 1, 1, common.ExitStatus("test"))

	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	tly1 := &tally{}
	tly2 := &tally{}

	ctx, cancel := context.WithTimeout(context.Background(), 500*time.Millisecond)
	defer cancel()

	ps := NewPubSub(log)

	ps.Subscribe(ctx, RASTypeRankStateChange, tly1)

	ps.Publish(evt1)
	ps.Publish(evt1)

	<-ctx.Done()
	ps.Reset()

	common.AssertStringsEqual(t, []string{
		RASTypeRankStateChange.String(), RASTypeRankStateChange.String(),
	}, tly1.getRx(), "unexpected slice of received events")
	common.AssertEqual(t, 0, len(tly2.getRx()), "unexpected number of received events")

	tly1.rx = make([]string, 0)
	tly2.rx = make([]string, 0)

	ctx2, cancel2 := context.WithTimeout(context.Background(), 500*time.Millisecond)
	defer cancel2()

	ps.Subscribe(ctx, RASTypeRankStateChange, tly2)

	ps.Publish(evt1)
	ps.Publish(evt1)

	<-ctx2.Done()
	ps.Close()

	common.AssertStringsEqual(t, []string{
		RASTypeRankStateChange.String(), RASTypeRankStateChange.String(),
	}, tly2.getRx(), "unexpected slice of received events")
	common.AssertEqual(t, 0, len(tly1.getRx()), "unexpected number of received events")
}
