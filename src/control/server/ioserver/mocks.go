//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ioserver

import (
	"context"
	"os"

	"github.com/daos-stack/daos/src/control/lib/atm"
)

type (
	TestRunnerConfig struct {
		StartCb    func()
		StartErr   error
		Running    atm.Bool
		SignalCb   func(uint32, os.Signal)
		SignalErr  error
		ErrChanCb  func() error
		ErrChanErr error
	}

	TestRunner struct {
		runnerCfg TestRunnerConfig
		serverCfg *Config
	}
)

func NewTestRunner(trc *TestRunnerConfig, sc *Config) *TestRunner {
	if trc == nil {
		trc = &TestRunnerConfig{}
	}
	return &TestRunner{
		runnerCfg: *trc,
		serverCfg: sc,
	}
}

func (tr *TestRunner) Start(ctx context.Context, errChan chan<- error) error {
	if tr.runnerCfg.StartCb != nil {
		tr.runnerCfg.StartCb()
	}
	if tr.runnerCfg.ErrChanCb == nil {
		tr.runnerCfg.ErrChanCb = func() error {
			return tr.runnerCfg.ErrChanErr
		}
	}

	go func() {
		select {
		case <-ctx.Done():
		case errChan <- tr.runnerCfg.ErrChanCb():
			if tr.runnerCfg.ErrChanErr != nil {
				tr.runnerCfg.Running.SetFalse()
			}
		}
	}()

	if tr.runnerCfg.StartErr == nil {
		tr.runnerCfg.Running.SetTrue()
	}

	return tr.runnerCfg.StartErr
}

func (tr *TestRunner) Signal(sig os.Signal) error {
	if tr.runnerCfg.SignalCb != nil {
		tr.runnerCfg.SignalCb(tr.serverCfg.Index, sig)
	}
	return tr.runnerCfg.SignalErr
}

func (tr *TestRunner) IsRunning() bool {
	return tr.runnerCfg.Running.IsTrue()
}

func (tr *TestRunner) GetConfig() *Config {
	return tr.serverCfg
}
