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

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"syscall"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

const (
	ioServerBin = "daos_io_server"

	// NormalExit indicates that the process exited without error
	NormalExit ExitStatus = "process exited with 0"
)

type (
	// ExitStatus implements the error interface and is
	// used to indicate special IOserver exit conditions.
	ExitStatus string

	// Runner starts and manages an instance of a DAOS I/O Server
	Runner struct {
		Config *Config
		log    logging.Logger
	}
)

func (es ExitStatus) Error() string {
	return string(es)
}

// Ensure that a monitored subcommand always returns
// an error of some sort when it exits so that we
// can respond appropriately.
func exitStatus(err error) error {
	if err != nil {
		return err
	}
	return NormalExit
}

// NewRunner returns a configured ioserver.Runner
func NewRunner(log logging.Logger, config *Config) *Runner {
	return &Runner{
		Config: config,
		log:    log,
	}
}

func (r *Runner) run(ctx context.Context, args, env []string) error {
	binPath, err := common.FindBinary(ioServerBin)
	if err != nil {
		return errors.Wrapf(err, "can't start %s", ioServerBin)
	}

	cmd := exec.CommandContext(ctx, binPath, args...)
	cmd.Stdout = &cmdLogger{
		logFn:  r.log.Info,
		prefix: fmt.Sprintf("%s:%d", ioServerBin, r.Config.Index),
	}
	cmd.Stderr = &cmdLogger{
		logFn:  r.log.Error,
		prefix: fmt.Sprintf("%s:%d", ioServerBin, r.Config.Index),
	}
	// FIXME(DAOS-3105): This shouldn't be the default. The command environment
	// should be constructed from values in the configuration. This probably
	// can't go away until PMIx support is removed, though.
	cmd.Env = mergeEnvVars(os.Environ(), env)

	cmd.SysProcAttr = &syscall.SysProcAttr{
		// I/O server should get a SIGKILL if this process dies.
		Pdeathsig: syscall.SIGKILL,
		// I/O server should run with real uid/gid (drop egid).
		Credential: &syscall.Credential{
			Uid:         uint32(os.Getuid()),
			Gid:         uint32(os.Getgid()),
			NoSetGroups: true,
		},
	}

	r.log.Debugf("%s:%d config: %#v", ioServerBin, r.Config.Index, r.Config)
	r.log.Debugf("%s:%d args: %s", ioServerBin, r.Config.Index, args)
	r.log.Debugf("%s:%d env: %s", ioServerBin, r.Config.Index, env)
	r.log.Infof("Starting I/O server instance %d: %s", r.Config.Index, binPath)
	return errors.Wrapf(exitStatus(cmd.Run()), "%s (instance %d) exited", binPath, r.Config.Index)
}

// Start asynchronously starts the IOServer instance
// and reports any errors on the output channel
func (r *Runner) Start(ctx context.Context, errOut chan<- error) error {
	args, err := r.Config.CmdLineArgs()
	if err != nil {
		return err
	}
	env, err := r.Config.CmdLineEnv()
	if err != nil {
		return err
	}

	go func() {
		errOut <- r.run(ctx, args, env)
	}()

	return nil
}

// GetConfig returns the runner's configuration
func (r *Runner) GetConfig() *Config {
	return r.Config
}
