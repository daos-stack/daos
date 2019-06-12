package main

import (
	"context"
	"os"
	"time"

	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/ioserver"
	"github.com/pkg/errors"
)

type managedInstance struct {
	cfg                      *ioserver.Config
	instance                 *ioserver.Instance
	superblock               *Superblock
	shouldReformatSuperblock bool
	fsRoot                   string
}

func (mi *managedInstance) Setup() (err error) {
	mi.superblock, err = mi.ReadSuperblock()
	if err == nil && !mi.shouldReformatSuperblock {
		return nil
	}

	if !os.IsNotExist(errors.Cause(err)) {
		if !mi.shouldReformatSuperblock {
			return errors.Wrap(err, "Failed to read existing superblock and reformat is false")
		}
	}

	if err := mi.CreateSuperblock(); err != nil {
		return errors.Wrap(err, "Setup failed to format superblock")
	}

	mi.superblock, err = mi.ReadSuperblock()
	return errors.Wrap(err, "Setup failed to read superblock after (re)-format")
}

func (mi *managedInstance) Start(ctx context.Context, errChan chan<- error) error {
	return mi.instance.Start(ctx, errChan)
}

func (mi *managedInstance) Shutdown(ctx context.Context) error {
	return mi.instance.Shutdown(ctx)
}

// IOServerHarness is responsible for managing IOServer instances
type IOServerHarness struct {
	instances []*managedInstance
}

// NewHarness returns an initialized *IOServerHarness
func NewIOServerHarness() *IOServerHarness {
	return &IOServerHarness{
		instances: make([]*managedInstance, 0, 2),
	}
}

// SetReady indicates that the harness should proceed
func (h *IOServerHarness) SetReady(r *srvpb.NotifyReadyReq) {
	// NOOP for now
	// FIXME: How should this interact with multiple instances?
	// FIXME: Should probably not be mixing protobuffer stuff in here
}

// AddInstance adds a new IOServer instance to be managed
func (h *IOServerHarness) AddInstance(cfg *ioserver.Config) {
	mi := &managedInstance{
		cfg:      cfg,
		instance: ioserver.NewInstance(cfg),
	}
	h.instances = append(h.instances, mi)
}

// StartInstances starts each of the configured instances and waits for
// them to exit.
func (h *IOServerHarness) StartInstances(parent context.Context) error {
	errChan := make(chan error, len(h.instances))
	for _, instance := range h.instances {
		if err := instance.Setup(); err != nil {
			return errors.Wrap(err, "Failed to setup instance")
		}
		if err := instance.Start(parent, errChan); err != nil {
			return err
		}
	}

	shutdown := func() {
		for _, instance := range h.instances {
			ctx, cancel := context.WithTimeout(context.Background(), 1*time.Second)
			if err := instance.Shutdown(ctx); err != nil {
				// TODO: Log shutdown error
			}
			defer cancel()
		}
	}

	for {
		select {
		case <-parent.Done():
			shutdown()
			return nil
		case err := <-errChan:
			// If we receive an error from any instance, shut them all down
			if err != nil {
				shutdown()
				return errors.Wrap(err, "Instance error")
			}
		}
	}
}
