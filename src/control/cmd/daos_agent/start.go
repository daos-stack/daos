//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"os"
	"os/signal"
	"path/filepath"
	"syscall"
	"time"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwloc"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwprov"
	"github.com/daos-stack/daos/src/control/lib/systemd"
)

type ctxKey string

const (
	agentSockName          = "daos_agent.sock"
	shuttingDownKey ctxKey = "agent_shutting_down"
)

func agentIsShuttingDown(ctx context.Context) bool {
	shuttingDown, ok := ctx.Value(shuttingDownKey).(*atm.Bool)
	if !ok {
		return false
	}
	return shuttingDown.IsTrue()
}

type startCmd struct {
	cmdutil.LogCmd
	configCmd
	ctlInvokerCmd
}

func (cmd *startCmd) Execute(_ []string) error {
	if err := common.CheckDupeProcess(); err != nil {
		return err
	}

	cmd.Infof("Starting %s (pid %d)", versionString(), os.Getpid())
	startedAt := time.Now()

	parent, shutdown := context.WithCancel(context.Background())
	defer shutdown()

	var shuttingDown atm.Bool
	ctx := context.WithValue(parent, shuttingDownKey, &shuttingDown)

	sockPath := filepath.Join(cmd.cfg.RuntimeDir, agentSockName)
	cmd.Debugf("Full socket path is now: %s", sockPath)

	// Agent socket file to be readable and writable by all.
	createDrpcStart := time.Now()
	drpcServer, err := drpc.NewDomainSocketServer(cmd.Logger, sockPath, 0666)
	if err != nil {
		cmd.Errorf("Unable to create socket server: %v", err)
		return err
	}
	cmd.Debugf("created dRPC server: %s", time.Since(createDrpcStart))

	hwprovInitStart := time.Now()
	hwprovFini, err := hwprov.Init(cmd.Logger)
	if err != nil {
		return err
	}
	defer hwprovFini()
	cmd.Debugf("initialized hardware providers: %s", time.Since(hwprovInitStart))

	cacheStart := time.Now()
	cache := NewInfoCache(ctx, cmd.Logger, cmd.ctlInvoker, cmd.cfg)
	if cmd.attachInfoCacheDisabled() {
		cache.DisableAttachInfoCache()
		cmd.Debug("GetAttachInfo agent caching has been disabled")
	}

	if cmd.fabricCacheDisabled() {
		cache.DisableFabricCache()
		cmd.Debug("Local fabric interface caching has been disabled")
	}
	cmd.Debugf("created cache: %s", time.Since(cacheStart))

	procmonStart := time.Now()
	procmon := NewProcMon(cmd.Logger, cmd.ctlInvoker, cmd.cfg.SystemName)
	procmon.startMonitoring(ctx)
	cmd.Debugf("started process monitor: %s", time.Since(procmonStart))

	drpcRegStart := time.Now()
	drpcServer.RegisterRPCModule(NewSecurityModule(cmd.Logger, cmd.cfg.TransportConfig))
	mgmtMod := &mgmtModule{
		log:        cmd.Logger,
		sys:        cmd.cfg.SystemName,
		ctlInvoker: cmd.ctlInvoker,
		cache:      cache,
		numaGetter: hwprov.DefaultProcessNUMAProvider(cmd.Logger),
		monitor:    procmon,
	}
	drpcServer.RegisterRPCModule(mgmtMod)
	cmd.Debugf("registered dRPC modules: %s", time.Since(drpcRegStart))

	hwlocStart := time.Now()
	// Cache hwloc data in context on startup, since it'll be used extensively at runtime.
	hwlocCtx, err := hwloc.CacheContext(ctx, cmd.Logger)
	if err != nil {
		return err
	}
	defer hwloc.Cleanup(hwlocCtx)
	cmd.Debugf("cached hwloc content: %s", time.Since(hwlocStart))

	drpcSrvStart := time.Now()
	err = drpcServer.Start(hwlocCtx)
	if err != nil {
		cmd.Errorf("Unable to start socket server on %s: %v", sockPath, err)
		return err
	}
	cmd.Debugf("dRPC socket server started: %s", time.Since(drpcSrvStart))

	cmd.Debugf("startup complete in %s", time.Since(startedAt))
	cmd.Infof("%s (pid %d) listening on %s", versionString(), os.Getpid(), sockPath)
	if err := systemd.Ready(); err != nil && err != systemd.ErrSdNotifyNoSocket {
		return errors.Wrap(err, "unable to notify systemd")
	}
	defer systemd.Stopping()

	// Setup signal handlers so we can block till we get SIGINT or SIGTERM
	signals := make(chan os.Signal)
	finish := make(chan struct{})

	signal.Notify(signals, syscall.SIGINT, syscall.SIGTERM, syscall.SIGPIPE, syscall.SIGUSR1, syscall.SIGUSR2)
	// Anonymous goroutine to wait on the signals channel and tell the
	// program to finish when it receives a signal. Since we notify on
	// SIGINT and SIGTERM we should only catch these on a kill or ctrl+c
	// SIGPIPE is caught and logged to avoid killing the agent.
	// The syntax looks odd but <- Channel means wait on any input on the
	// channel.
	var shutdownRcvd time.Time
	go func() {
		for sig := range signals {
			switch sig {
			case syscall.SIGPIPE:
				cmd.Infof("Signal received.  Caught non-fatal %s; continuing", sig)
			case syscall.SIGUSR1:
				cmd.Infof("Signal received.  Caught %s; flushing open pool handles", sig)
				procmon.FlushAllHandles(ctx)
			case syscall.SIGUSR2:
				cmd.Infof("Signal received. Caught %s; refreshing caches", sig)
				mgmtMod.RefreshCache(ctx)
			default:
				shutdownRcvd = time.Now()
				cmd.Infof("Signal received.  Caught %s; shutting down", sig)
				shuttingDown.SetTrue()
				if !cmd.cfg.DisableAutoEvict {
					procmon.FlushAllHandles(ctx)
				}
				close(finish)
				return
			}
		}
	}()
	<-finish

	cmd.Debugf("shutdown complete in %s", time.Since(shutdownRcvd))
	return nil
}

func (cmd *startCmd) attachInfoCacheDisabled() bool {
	return cmd.cfg.DisableCache || os.Getenv("DAOS_AGENT_DISABLE_CACHE") == "true"
}

func (cmd *startCmd) fabricCacheDisabled() bool {
	return cmd.cfg.DisableCache || os.Getenv("DAOS_AGENT_DISABLE_OFI_CACHE") == "true"
}
