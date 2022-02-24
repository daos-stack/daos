//
// (C) Copyright 2019-2022 Intel Corporation.
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
	Config   *Config
	log      logging.Logger
	running  atm.Bool
	sigCh    chan os.Signal
	sigErrCh chan error
	pidCh    chan int
}

// NewRunner returns a configured engine.Runner
func NewRunner(log logging.Logger, config *Config) *Runner {
	return &Runner{
		Config:   config,
		log:      log,
		sigCh:    make(chan os.Signal),
		sigErrCh: make(chan error),
	}
}

func (r *Runner) run(ctx context.Context, args, env []string, errOut chan<- error) error {
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

	r.pidCh = make(chan int, 1)
	cmdDone := make(chan struct{})
	// Start a goroutine to wait for the command to finish.
	go func() {
		r.running.SetTrue()
		defer r.running.SetFalse()

		errOut <- errors.Wrapf(common.GetExitStatus(cmd.Wait()), "%s exited", cmd.Path)
		r.pidCh <- cmd.ProcessState.Pid()
		close(r.pidCh)
		close(cmdDone)
	}()

	// Start a monitoring goroutine to handle context cancellation, signals, and command completion.
	go func() {
		for {
			select {
			case <-ctx.Done():
				r.log.Infof("%s:%d context canceled; shutting down", engineBin, r.Config.Index)
				if err := cmd.Process.Signal(syscall.SIGTERM); err != nil {
					r.log.Errorf("%s:%d failed to cleanly kill process: %s", engineBin, r.Config.Index, err)
					cmd.Process.Signal(syscall.SIGKILL)
				}
				return
			case sig := <-r.sigCh:
				r.log.Infof("%s:%d received signal %s", engineBin, r.Config.Index, sig)
				var err error
				if err = cmd.Process.Signal(sig); err != nil {
					r.log.Errorf("%s:%d failed to send signal %s to process: %s", engineBin, r.Config.Index, sig, err)
				}
				r.sigErrCh <- err
			case <-cmdDone:
				return
			}
		}
	}()

	return nil
}

// Start asynchronously starts the Engine instance.
func (r *Runner) Start(ctx context.Context, errOut chan<- error) error {
	args, err := r.Config.CmdLineArgs()
	if err != nil {
		return err
	}
	env, err := r.Config.CmdLineEnv()
	if err != nil {
		return err
	}
	env = mergeEnvVars(cleanEnvVars(os.Environ(), r.Config.EnvPassThrough), env)

	return r.run(ctx, args, env, errOut)
}

// IsRunning indicates whether the Runner process is running or not.
func (r *Runner) IsRunning() bool {
	return r.running.Load()
}

// Signal sends relevant signal to the Runner process (idempotent).
func (r *Runner) Signal(signal os.Signal) error {
	if !r.IsRunning() {
		return nil
	}

	r.sigCh <- signal
	return <-r.sigErrCh
}

// GetLastPid returns the PID after runner has exited. It
// is usually an error to call this with a running process,
// as the PID is not available on the channel until the
// process exits, and therefore the call may block.
func (r *Runner) GetLastPid() uint64 {
	return uint64(<-r.pidCh)
}

// GetConfig returns the runner's configuration
func (r *Runner) GetConfig() *Config {
	return r.Config
}
