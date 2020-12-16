//
// (C) Copyright 2020 Intel Corporation.
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

	enabled := atm.NewBool(os.Getenv("DAOS_AGENT_DISABLE_CACHE") != "true")
	if enabled.IsFalse() {
		cmd.log.Debugf("GetAttachInfo agent caching has been disabled\n")
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

	procmon := NewProcMon(cmd.log)
	procmon.startMonitoring(ctx)

	drpcServer.RegisterRPCModule(NewSecurityModule(cmd.log, cmd.cfg.TransportConfig))
	drpcServer.RegisterRPCModule(&mgmtModule{
		log:        cmd.log,
		sys:        cmd.cfg.SystemName,
		ctlInvoker: cmd.ctlInvoker,
		aiCache:    &attachInfoCache{log: cmd.log, enabled: enabled},
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
