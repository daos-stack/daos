//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package engine

import (
	"context"
	"os"

	"github.com/daos-stack/daos/src/control/lib/atm"
)

// MockConfig returns an I/O Engine config set up for testing.
func MockConfig() *Config {
	return &Config{
		HelperStreamCount: maxHelperStreamCount,
	}
}

type (
	TestRunnerConfig struct {
		StartCb          func()
		StartErr         error
		Running          atm.Bool
		SignalCb         func(uint32, os.Signal)
		LastPid          uint64
		RunnerExitInfoCb func(context.Context) *RunnerExitInfo
		RunnerExitInfo   *RunnerExitInfo
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

func (tr *TestRunner) Start(ctx context.Context) (RunnerExitChan, error) {
	if tr.runnerCfg.StartCb != nil {
		tr.runnerCfg.StartCb()
	}
	if tr.runnerCfg.RunnerExitInfoCb == nil {
		tr.runnerCfg.RunnerExitInfoCb = func(_ context.Context) *RunnerExitInfo {
			return tr.runnerCfg.RunnerExitInfo
		}
	}

	runnerExitInfoCh := make(chan *RunnerExitInfo)
	go func() {
		select {
		case <-ctx.Done():
		case runnerExitInfoCh <- tr.runnerCfg.RunnerExitInfoCb(ctx):
			tr.runnerCfg.Running.SetFalse()
		}
	}()

	if tr.runnerCfg.StartErr == nil {
		tr.runnerCfg.Running.SetTrue()
	}

	return runnerExitInfoCh, tr.runnerCfg.StartErr
}

func (tr *TestRunner) Signal(sig os.Signal) {
	if tr.runnerCfg.SignalCb != nil {
		tr.runnerCfg.SignalCb(tr.serverCfg.Index, sig)
	}
}

func (tr *TestRunner) IsRunning() bool {
	return tr.runnerCfg.Running.IsTrue()
}

func (tr *TestRunner) GetLastPid() uint64 {
	return tr.runnerCfg.LastPid
}

func (tr *TestRunner) GetConfig() *Config {
	return tr.serverCfg
}

func (tr *TestRunner) GetRunnerConfig() *TestRunnerConfig {
	return &tr.runnerCfg
}
