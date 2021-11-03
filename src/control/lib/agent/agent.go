//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package agent

import (
	"context"
	"fmt"
	"os"
	"os/signal"
	"path/filepath"
	"syscall"
	"time"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
)

const (
	agentSockName = "daos_agent.sock"
)

type (
	Server struct {
		cfg       Config
		ctlClient control.Invoker
	}
)

func NewServer(cfg *Config, ctlClient control.Invoker) *Server {
	if cfg == nil {
		cfg = DefaultConfig()
	}

	return &Server{
		cfg:       *cfg,
		ctlClient: ctlClient,
	}
}

func versionString() string {
	return fmt.Sprintf("%s v%s", build.AgentName, build.DaosVersion)
}

func (s *Server) Start(ctx context.Context, log logging.Logger) error {
	log.Debugf("Starting %s (pid %d)", versionString(), os.Getpid())
	startedAt := time.Now()

	sockPath := filepath.Join(s.cfg.RuntimeDir, agentSockName)
	log.Debugf("Full socket path is now: %s", sockPath)

	drpcServer, err := drpc.NewDomainSocketServer(ctx, log, sockPath)
	if err != nil {
		log.Errorf("Unable to create socket server: %v", err)
		return err
	}

	aicEnabled := (os.Getenv("DAOS_AGENT_DISABLE_CACHE") != "true")
	if !aicEnabled {
		log.Debugf("GetAttachInfo agent caching has been disabled\n")
	}

	ficEnabled := (os.Getenv("DAOS_AGENT_DISABLE_OFI_CACHE") != "true")
	if !ficEnabled {
		log.Debugf("Local fabric interface caching has been disabled\n")
	}

	netCtx, err := netdetect.Init(ctx)
	defer netdetect.CleanUp(netCtx)
	if err != nil {
		log.Errorf("Unable to initialize netdetect services")
		return err
	}

	numaAware := netdetect.HasNUMA(netCtx)
	if !numaAware {
		log.Debugf("This system is not NUMA aware.  Any devices found are reported as NUMA node 0.")
	}

	procmon := NewProcMon(log, s.ctlClient, s.cfg.SystemName)
	procmon.startMonitoring(ctx)

	fabricCache := newLocalFabricCache(log, ficEnabled)
	if len(s.cfg.FabricInterfaces) > 0 {
		// Cache is required to use user-defined fabric interfaces
		fabricCache.enabled.SetTrue()
		nf := NUMAFabricFromConfig(log, s.cfg.FabricInterfaces)
		fabricCache.Cache(ctx, nf)
	}

	drpcServer.RegisterRPCModule(NewSecurityModule(log, s.cfg.TransportConfig))
	drpcServer.RegisterRPCModule(&mgmtModule{
		log:        log,
		sys:        s.cfg.SystemName,
		ctlInvoker: s.ctlClient,
		attachInfo: newAttachInfoCache(log, aicEnabled),
		fabricInfo: fabricCache,
		numaAware:  numaAware,
		netCtx:     netCtx,
		monitor:    procmon,
	})

	err = drpcServer.Start()
	if err != nil {
		log.Errorf("Unable to start socket server on %s: %v", sockPath, err)
		return err
	}

	log.Debugf("startup complete in %s", time.Since(startedAt))
	log.Infof("%s (pid %d) listening on %s", versionString(), os.Getpid(), sockPath)

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
			log.Infof("Signal received.  Caught non-fatal %s; continuing", sig)
		default:
			shutdownRcvd = time.Now()
			log.Infof("Signal received.  Caught %s; shutting down", sig)
			close(finish)
		}
	}()
	<-finish
	drpcServer.Shutdown()

	log.Debugf("shutdown complete in %s", time.Since(shutdownRcvd))

	return nil
}
