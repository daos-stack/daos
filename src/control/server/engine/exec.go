//
// (C) Copyright 2019-2021 Intel Corporation.
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
	cmd     *exec.Cmd
}

// NewRunner returns a configured engine.Runner
func NewRunner(log logging.Logger, config *Config) *Runner {
	return &Runner{
		Config: config,
		log:    log,
	}
}

func (r *Runner) run(ctx context.Context, args, env []string) error {
	binPath, err := common.FindBinary(engineBin)
	if err != nil {
		return errors.Wrapf(err, "can't start %s", engineBin)
	}

	cmd := exec.CommandContext(ctx, binPath, args...)
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
	r.cmd = cmd

	r.running.SetTrue()
	defer r.running.SetFalse()

	return errors.Wrapf(common.GetExitStatus(cmd.Wait()),
		"%s (instance %d) exited", binPath, r.Config.Index)
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
	env = mergeEnvVars(env, cleanEnvVars(os.Environ(), r.Config.EnvPassThrough))

	go func() {
		errOut <- r.run(ctx, args, env)
	}()

	return nil
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

	r.log.Debugf("Signalling I/O Engine instance %d (%s)", r.Config.Index, signal)

	return r.cmd.Process.Signal(signal)
}

// GetConfig returns the runner's configuration
func (r *Runner) GetConfig() *Config {
	return r.Config
}
