//
// (C) Copyright 2020-2022 Intel Corporation.
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

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwloc"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwprov"
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
	logCmd
	configCmd
	ctlInvokerCmd
}

func (cmd *startCmd) Execute(_ []string) error {
	cmd.log.Debugf("Starting %s (pid %d)", versionString(), os.Getpid())
	startedAt := time.Now()

	parent, shutdown := context.WithCancel(context.Background())
	defer shutdown()

	var shuttingDown atm.Bool
	ctx := context.WithValue(parent, shuttingDownKey, &shuttingDown)

	sockPath := filepath.Join(cmd.cfg.RuntimeDir, agentSockName)
	cmd.log.Debugf("Full socket path is now: %s", sockPath)

	drpcServer, err := drpc.NewDomainSocketServer(cmd.log, sockPath)
	if err != nil {
		cmd.log.Errorf("Unable to create socket server: %v", err)
		return err
	}

	aicEnabled := !cmd.attachInfoCacheDisabled()
	if !aicEnabled {
		cmd.log.Debugf("GetAttachInfo agent caching has been disabled")
	}

	ficEnabled := !cmd.fabricCacheDisabled()
	if !ficEnabled {
		cmd.log.Debugf("Local fabric interface caching has been disabled")
	}

	hwprovFini, err := hwprov.Init(cmd.log)
	if err != nil {
		return err
	}
	defer hwprovFini()

	procmon := NewProcMon(cmd.log, cmd.ctlInvoker, cmd.cfg.SystemName)
	procmon.startMonitoring(ctx)

	fabricCache := newLocalFabricCache(cmd.log, ficEnabled)
	if len(cmd.cfg.FabricInterfaces) > 0 {
		// Cache is required to use user-defined fabric interfaces
		fabricCache.enabled.SetTrue()
		nf := NUMAFabricFromConfig(cmd.log, cmd.cfg.FabricInterfaces)
		fabricCache.Cache(ctx, nf)
	}

	drpcServer.RegisterRPCModule(NewSecurityModule(cmd.log, cmd.cfg.TransportConfig))
	drpcServer.RegisterRPCModule(&mgmtModule{
		log:            cmd.log,
		sys:            cmd.cfg.SystemName,
		ctlInvoker:     cmd.ctlInvoker,
		attachInfo:     newAttachInfoCache(cmd.log, aicEnabled),
		fabricInfo:     fabricCache,
		numaGetter:     hwprov.DefaultProcessNUMAProvider(cmd.log),
		fabricScanner:  hwprov.DefaultFabricScanner(cmd.log),
		devClassGetter: hwprov.DefaultNetDevClassProvider(cmd.log),
		devStateGetter: hwprov.DefaultNetDevStateProvider(cmd.log),
		monitor:        procmon,
	})

	// Cache hwloc data in context on startup, since it'll be used extensively at runtime.
	hwlocCtx, err := hwloc.CacheContext(ctx, cmd.log)
	if err != nil {
		return err
	}
	defer hwloc.Cleanup(hwlocCtx)

	err = drpcServer.Start(hwlocCtx)
	if err != nil {
		cmd.log.Errorf("Unable to start socket server on %s: %v", sockPath, err)
		return err
	}

	cmd.log.Debugf("startup complete in %s", time.Since(startedAt))
	cmd.log.Infof("%s (pid %d) listening on %s", versionString(), os.Getpid(), sockPath)

	// Setup signal handlers so we can block till we get SIGINT or SIGTERM
	signals := make(chan os.Signal)
	finish := make(chan struct{})

	signal.Notify(signals, syscall.SIGINT, syscall.SIGTERM, syscall.SIGPIPE, syscall.SIGUSR1)
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
				cmd.log.Infof("Signal received.  Caught non-fatal %s; continuing", sig)
			case syscall.SIGUSR1:
				cmd.log.Infof("Signal received.  Caught %s; flushing open pool handles", sig)
				procmon.FlushAllHandles(ctx)
			default:
				shutdownRcvd = time.Now()
				cmd.log.Infof("Signal received.  Caught %s; shutting down", sig)
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

	cmd.log.Debugf("shutdown complete in %s", time.Since(shutdownRcvd))
	return nil
}

func (cmd *startCmd) attachInfoCacheDisabled() bool {
	return cmd.cfg.DisableCache || os.Getenv("DAOS_AGENT_DISABLE_CACHE") == "true"
}

func (cmd *startCmd) fabricCacheDisabled() bool {
	return cmd.cfg.DisableCache || os.Getenv("DAOS_AGENT_DISABLE_OFI_CACHE") == "true"
}
