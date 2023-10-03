//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package engine

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"syscall"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/logging"
)

const engineBin = "daos_engine"

// Runner starts and manages an instance of a DAOS I/O Engine
type Runner struct {
	Config  *Config
	log     logging.Logger
	running atm.Bool
	sigCh   chan os.Signal
}

type (
	RunnerExitInfo struct {
		Error error
		PID   int
	}

	RunnerExitChan chan *RunnerExitInfo
)

// NewRunner returns a configured engine.Runner
func NewRunner(log logging.Logger, config *Config) *Runner {
	return &Runner{
		Config: config,
		log:    log,
		sigCh:  make(chan os.Signal, 1),
	}
}

func (r *Runner) run(parent context.Context, args, env []string, exitCh RunnerExitChan) error {
	binPath, err := common.FindBinary(engineBin)
	if err != nil {
		return errors.Wrapf(err, "can't start %s", engineBin)
	}

	cmd := exec.Command(binPath, args...)
	cmd.Stdout = &cmdLogger{
		logFn:  r.log.Info,
		prefix: fmt.Sprintf("%s:%d", engineBin, r.Config.Index),
	}
	cmd.Stderr = &cmdLogger{
		logFn:  r.log.Error,
		prefix: fmt.Sprintf("%s:%d", engineBin, r.Config.Index),
	}
	cmd.Env = env

	cmd.SysProcAttr = &syscall.SysProcAttr{
		// I/O Engine should get a SIGKILL if this process dies.
		Pdeathsig: syscall.SIGKILL,
		// I/O Engine should run with real uid/gid (drop egid).
		Credential: &syscall.Credential{
			Uid:         uint32(os.Getuid()),
			Gid:         uint32(os.Getgid()),
			NoSetGroups: true,
		},
	}

	r.log.Debugf("%s:%d args: %s", engineBin, r.Config.Index, args)
	r.log.Debugf("%s:%d env: %s", engineBin, r.Config.Index, cmd.Env)
	r.log.Infof("Starting I/O Engine instance %d: %s", r.Config.Index, binPath)

	if err := cmd.Start(); err != nil {
		return errors.Wrapf(common.GetExitStatus(err),
			"%s (instance %d) failed to start", binPath, r.Config.Index)
	}
	r.running.SetTrue()

	ctx, cancel := context.WithCancel(parent)
	go func() {
		// Block on cmd.Wait() and then cancel the inner context
		// to signal that the process has completed.
		exitInfo := &RunnerExitInfo{
			Error: errors.Wrapf(common.GetExitStatus(cmd.Wait()), "%s exited", binPath),
			PID:   cmd.Process.Pid,
		}
		cancel()
		r.running.SetFalse()

		// Send the exit info to the exit channel for any interested readers.
		exitCh <- exitInfo
		close(exitCh)
	}()

	go func() {
		for {
			select {
			case <-parent.Done():
				r.log.Debugf("parent context cancelled, killing %s:%d", engineBin, r.Config.Index)
				if err := cmd.Process.Signal(syscall.SIGKILL); err != nil && err != os.ErrProcessDone {
					r.log.Errorf("%s:%d failed to kill pid %d: %s", engineBin, r.Config.Index, cmd.Process.Pid, err)
				}
			case <-ctx.Done():
				return
			case sig := <-r.sigCh:
				r.log.Debugf("signalling %s:%d with %s", engineBin, r.Config.Index, sig)
				if err := cmd.Process.Signal(sig); err != nil {
					r.log.Errorf("%s:%d failed to send signal %s to pid %d: %s", engineBin, r.Config.Index, sig, cmd.Process.Pid, err)
				}
			}
		}
	}()

	return nil
}

// Try to integrate DD_SUBSYS into D_LOG_MASK then unset DD_SUBSYS in environment.
func processLogEnvs(env []string) ([]string, error) {
	subsys, err := common.FindKeyValue(env, envLogSubsystems)
	if err != nil || subsys == "" {
		return env, nil // No DD_SUBSYS to process.
	}

	logMasks, err := common.FindKeyValue(env, envLogMasks)
	if err != nil || logMasks == "" {
		return env, nil // No D_LOG_MASK to process.
	}

	newLogMasks, err := MergeLogEnvVars(logMasks, subsys)
	if err != nil {
		return nil, err
	}
	if newLogMasks == "" {
		return nil, errors.New("empty log masks string is invalid")
	}

	env, err = common.UpdateKeyValue(env, envLogMasks, newLogMasks)
	if err != nil {
		return nil, err
	}

	env, err = common.DeleteKeyValue(env, envLogSubsystems)
	if err != nil && !errors.Is(err, os.ErrNotExist) {
		return nil, err
	}

	return env, nil
}

// Start asynchronously starts the Engine instance.
func (r *Runner) Start(ctx context.Context) (RunnerExitChan, error) {
	args, err := r.Config.CmdLineArgs()
	if err != nil {
		return nil, err
	}
	env, err := r.Config.CmdLineEnv()
	if err != nil {
		return nil, err
	}
	env = common.MergeKeyValues(cleanEnvVars(os.Environ(), r.Config.EnvPassThrough), env)

	env, err = processLogEnvs(env)
	if err != nil {
		return nil, err
	}

	exitCh := make(RunnerExitChan)
	return exitCh, r.run(ctx, args, env, exitCh)
}

// IsRunning indicates whether the Runner process is running or not.
func (r *Runner) IsRunning() bool {
	return r.running.Load()
}

// Signal sends relevant signal to the Runner process (idempotent).
func (r *Runner) Signal(signal os.Signal) {
	if !r.IsRunning() {
		return
	}

	r.log.Debugf("Signalling I/O Engine instance %d (%s)", r.Config.Index, signal)
	r.sigCh <- signal
}

// GetConfig returns the runner's configuration
func (r *Runner) GetConfig() *Config {
	return r.Config
}
