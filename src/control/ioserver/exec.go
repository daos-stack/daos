// +build linux

package ioserver

import (
	"context"
	"os"
	"os/exec"
	"syscall"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/log"
)

const binaryName = "daos_io_server"

func (srv *Instance) run(ctx context.Context, args, env []string) error {
	binaryPath, err := exec.LookPath(binaryName)
	if err != nil {
		return errors.Wrapf(err, "Unable to find %s in path", binaryName)
	}

	cmd := exec.CommandContext(ctx, binaryPath, args...)
	// TODO: Configure stdout/stderr to log
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Env = env

	// I/O server should get a SIGKILL if this process dies.
	cmd.SysProcAttr = &syscall.SysProcAttr{
		Pdeathsig: syscall.SIGKILL,
	}

	log.Debugf("Starting instance: %s %s", binaryPath, args)
	log.Debugf("Env: %s", env)
	//signal.Notify(srv.sigchld, syscall.SIGCHLD)
	return errors.Wrapf(cmd.Run(), "%s exited", binaryPath)
}
