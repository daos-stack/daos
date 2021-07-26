//
// (C) Copyright 2020-2021 Intel Corporation.
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
	"github.com/daos-stack/daos/src/control/lib/netdetect"
)

const (
	agentSockName = "daos_agent.sock"
)

type startCmd struct {
	logCmd
	configCmd
	ctlInvokerCmd
}

func (cmd *startCmd) Execute(_ []string) error {
	cmd.log.Debugf("Starting %s (pid %d)", versionString(), os.Getpid())
	startedAt := time.Now()

	ctx, shutdown := context.WithCancel(context.Background())
	defer shutdown()

	sockPath := filepath.Join(cmd.cfg.RuntimeDir, agentSockName)
	cmd.log.Debugf("Full socket path is now: %s", sockPath)

	drpcServer, err := drpc.NewDomainSocketServer(ctx, cmd.log, sockPath)
	if err != nil {
		cmd.log.Errorf("Unable to create socket server: %v", err)
		return err
	}

	aicEnabled := (os.Getenv("DAOS_AGENT_DISABLE_CACHE") != "true")
	if !aicEnabled {
		cmd.log.Debugf("GetAttachInfo agent caching has been disabled\n")
	}

	ficEnabled := (os.Getenv("DAOS_AGENT_DISABLE_OFI_CACHE") != "true")
	if !ficEnabled {
		cmd.log.Debugf("Local fabric interface caching has been disabled\n")
	}

	netCtx, err := netdetect.Init(context.Background())
	defer netdetect.CleanUp(netCtx)
	if err != nil {
		cmd.log.Errorf("Unable to initialize netdetect services")
		return err
	}

	numaAware := netdetect.HasNUMA(netCtx)
	if !numaAware {
		cmd.log.Debugf("This system is not NUMA aware.  Any devices found are reported as NUMA node 0.")
	}

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
		log:        cmd.log,
		sys:        cmd.cfg.SystemName,
		ctlInvoker: cmd.ctlInvoker,
		attachInfo: newAttachInfoCache(cmd.log, aicEnabled),
		fabricInfo: newLocalFabricCache(cmd.log, ficEnabled),
		numaAware:  numaAware,
		netCtx:     netCtx,
		monitor:    procmon,
	})

	err = drpcServer.Start()
	if err != nil {
		cmd.log.Errorf("Unable to start socket server on %s: %v", sockPath, err)
		return err
	}

	cmd.log.Debugf("startup complete in %s", time.Since(startedAt))
	cmd.log.Infof("%s (pid %d) listening on %s", versionString(), os.Getpid(), sockPath)

	// Setup signal handlers so we can block till we get SIGINT or SIGTERM
	signals := make(chan os.Signal)
	finish := make(chan struct{})

	signal.Notify(signals, syscall.SIGINT, syscall.SIGTERM, syscall.SIGPIPE)
	// Anonymous goroutine to wait on the signals channel and tell the
	// program to finish when it receives a signal. Since we notify on
	// SIGINT and SIGTERM we should only catch these on a kill or ctrl+c
	// SIGPIPE is caught and logged to avoid killing the agent.
	// The syntax looks odd but <- Channel means wait on any input on the
	// channel.
	var shutdownRcvd time.Time
	go func() {
		sig := <-signals
		switch sig {
		case syscall.SIGPIPE:
			cmd.log.Infof("Signal received.  Caught non-fatal %s; continuing", sig)
		default:
			shutdownRcvd = time.Now()
			cmd.log.Infof("Signal received.  Caught %s; shutting down", sig)
			close(finish)
		}
	}()
	<-finish
	drpcServer.Shutdown()

	cmd.log.Debugf("shutdown complete in %s", time.Since(shutdownRcvd))
	return nil
}
