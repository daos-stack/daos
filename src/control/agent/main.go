//
// (C) Copyright 2018-2019 Intel Corporation.
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
	"flag"
	"os"
	"os/signal"
	"path/filepath"
	"syscall"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/log"
)

var (
	runtimeDir = flag.String("runtime_dir", "/var/run/daos_agent", "The path to runtime socket directory for daos_agent")
	logFile    = flag.String("logfile", "/tmp/daos_agent.log", "Path for the daos agent log file")
)

func main() {
	var err error
	defer func() {
		status := 0
		if err != nil {
			status = 1
		}
		os.Exit(status)
	}()

	// Set default global logger for application.
	log.NewDefaultLogger(log.Debug, "", os.Stderr)

	log.Debugf("Starting daos_agent:")

	flag.Parse()

	f, err := common.AppendFile(*logFile)
	if err != nil {
		log.Errorf("Failure creating log file: %s", err)
		return
	}
	defer f.Close()
	log.SetOutput(f)

	// Setup signal handlers so we can block till we get SIGINT or SIGTERM
	signals := make(chan os.Signal, 1)
	finish := make(chan bool, 1)
	signal.Notify(signals, syscall.SIGINT, syscall.SIGTERM)

	sockPath := filepath.Join(*runtimeDir, "agent.sock")
	drpcServer, err := drpc.NewDomainSocketServer(sockPath)
	if err != nil {
		log.Errorf("Unable to create socket server: %v", err)
		return
	}

	module := &SecurityModule{}
	drpcServer.RegisterRPCModule(module)

	err = drpcServer.Start()
	if err != nil {
		log.Errorf("Unable to start socket server on %s: %v", sockPath, err)
		return
	}

	log.Debugf("Listening on %s", sockPath)

	// Anonymous goroutine to wait on the signals channel and tell the
	// program to finish when it receives a signal. Since we only notify on
	// SIGINT and SIGTERM we should only catch this on a kill or ctrl+c
	// The syntax looks odd but <- Channel means wait on any input on the
	// channel.
	go func() {
		<-signals
		finish <- true
	}()
	<-finish
	drpcServer.Shutdown()
}
