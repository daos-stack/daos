//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package checker

import (
	"context"

	"github.com/pkg/errors"

	chkpb "github.com/daos-stack/daos/src/control/common/proto/chk"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/logging"
)

type (
	Status struct{}

	StateStore interface {
		FindingStore
		ResetCheckerData() error
		GetCheckerState() (State, error)
		//AdvanceCheckerPass() (chkpb.CheckScanPhase, error)
	}

	SystemSourceStore interface {
		StateStore
	}

	Coordinator struct {
		running  atm.Bool
		log      logging.Logger
		db       StateStore
		checkers []Checker
		cmdCh    chan bool
	}
)

func NewCoordinator(log logging.Logger, store SystemSourceStore, checkers ...Checker) *Coordinator {
	return &Coordinator{
		log:      log,
		db:       store,
		checkers: checkers,
		cmdCh:    make(chan bool),
	}
}

func DefaultCoordinator(log logging.Logger, store SystemSourceStore, engine EngineChecker) *Coordinator {
	return NewCoordinator(log, store)
}

func (cc *Coordinator) runLoop(ctx context.Context) {
	cc.log.Debug("starting checker coordinator loop")
	defer cc.running.SetFalse()

	for {
		select {
		case <-ctx.Done():
			cc.log.Info("shutting down checker coordinator")
			return
		case start := <-cc.cmdCh:
			if !start {
				continue
			}
			state, err := cc.db.GetCheckerState()
			if err != nil {
				cc.log.Errorf("error getting checker state: %v", err)
				continue
			}

			cc.log.Debugf("received request to run checkers (current pass: %s)", state.CurrentPass)
			if err := cc.runCheckers(ctx); err != nil {
				if !IsPassFindings(err) {
					cc.log.Errorf("error running checkers: %s", err)
				}
				cc.log.Infof("checker paused at pass %s: %s", state.CurrentPass, err)
			}
		}
	}
}

func (cc *Coordinator) runCheckers(ctx context.Context) error {
	for {
		findings, err := cc.db.GetCheckerFindings()
		if err != nil {
			return err
		}

		if len(findings) > 0 {
			return ErrPassFindings
		}

		/*pass, err := cc.db.AdvanceCheckerPass()
		if err != nil {
			if errors.Is(err, ErrNoMorePasses) {
				cc.log.Debug("no more passes to run")
				break
			}
			return err
		}

		cc.log.Debugf("running checker pass %d", pass)

		for _, checker := range cc.checkers {
			if err := checker.RunPassChecks(ctx, pass, cc.db); err != nil {
				return err
			}
		}*/
	}

	return nil
}

func (cc *Coordinator) RunCheckers() error {
	if !cc.running.IsTrue() {
		return errors.New("checker coordinator not running")
	}

	cc.cmdCh <- true

	return nil
}

func (cc *Coordinator) Start(ctx context.Context) error {
	state, err := cc.db.GetCheckerState()
	if err != nil {
		return err
	}

	if cc.running.IsTrue() {
		return errors.New("checker coordinator already running")
	}
	cc.running.SetTrue()

	//go cc.runLoop(ctx)

	// Restart the process if was already running before it
	// started on this node.
	if state.CurrentPass > chkpb.CheckScanPhase_CSP_PREPARE {
		cc.log.Infof("restarting checker coordinator from pass %d", state.CurrentPass)
		return cc.RunCheckers()
	}

	return nil
}
