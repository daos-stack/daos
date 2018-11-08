//
// (C) Copyright 2018 Intel Corporation.
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
	"log"
	"os"
	"os/signal"
	"syscall"

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/drpc_test/hello"
)

var (
	unixSocket = flag.String("unix_socket", "./drpc_test.sock", "The path to the unix socket to be used for drpc messages")
)

func main() {
	flag.Parse()

	// Setup signal handlers so we can block till we get SIGINT or SIGTERM
	signals := make(chan os.Signal, 1)
	finish := make(chan bool, 1)
	signal.Notify(signals, syscall.SIGINT, syscall.SIGTERM)

	drpcServer, err := drpc.NewDomainSocketServer(*unixSocket)
	if err != nil {
		log.Fatalf("Unable to create socket server: %v", err)
	}

	module := &hello.HelloModule{}
	drpcServer.RegisterRPCModule(module)

	err = drpcServer.Start()
	if err != nil {
		log.Fatalf("Unable to start socket server on %s: %v", *unixSocket, err)
	}

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
}
