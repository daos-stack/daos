package ioserver

import (
	"context"
)

// Instance is an instance of a DAOS IO Server
type Instance struct {
	cfg *Config
}

// NewInstance returns a configured ioserver.Instance
func NewInstance(config *Config) *Instance {
	return &Instance{
		cfg: config,
	}
}

// Start asynchronously starts the IOServer instance
// and reports any errors on the output channel
func (srv *Instance) Start(ctx context.Context, errOut chan<- error) error {
	args, err := srv.cfg.CmdLineArgs()
	if err != nil {
		return err
	}
	env, err := srv.cfg.EnvVars()
	if err != nil {
		return err
	}

	go func() {
		errOut <- srv.run(ctx, args, env)
	}()

	return nil
}

func (srv *Instance) Shutdown(ctx context.Context) error {
	return nil
}
