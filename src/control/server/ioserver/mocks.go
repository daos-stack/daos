//
// (C) Copyright 2019 Intel Corporation.
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
package ioserver

import "context"

type (
	TestRunnerConfig struct {
		StartCb    func()
		StartErr   error
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
			return
		case errChan <- tr.runnerCfg.ErrChanCb():
			return
		}
	}()

	return tr.runnerCfg.StartErr
}

func (tr *TestRunner) IsStarted() bool { return true }

func (tr *TestRunner) GetConfig() *Config {
	return tr.serverCfg
}
