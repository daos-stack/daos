//
// (C) Copyright 2018-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"flag"
	"fmt"
	"os"
	"os/signal"
	"syscall"

	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/cmd/drpc_test/hello"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
)

var (
	unixSocket = flag.String("unix_socket", "./drpc_test.sock", "The path to the unix socket to be used for drpc messages")
	server     = flag.Bool("server", false, "Start up a dRPC test server. Otherwise, will run in client mode")
)

func main() {
	log := logging.NewCommandLineLogger()
	flag.Parse()

	var err error
	if *server {
		err = runDrpcServer(log)
	} else {
		err = runDrpcClient(log)
	}
	status := 0
	if err != nil {
		log.Errorf(err.Error())
		status = 1
	}
	os.Exit(status)
}

func runDrpcServer(log logging.Logger) error {
	// Setup signal handlers so we can block till we get SIGINT or SIGTERM
	signals := make(chan os.Signal, 1)
	finish := make(chan bool, 1)
	signal.Notify(signals, syscall.SIGINT, syscall.SIGTERM)

	ctx, shutdown := context.WithCancel(context.Background())
	defer shutdown()

	drpcServer, err := drpc.NewDomainSocketServer(log, *unixSocket)
	if err != nil {
		return errors.Wrap(err, "creating socket server")
	}

	module := &hello.HelloModule{}
	drpcServer.RegisterRPCModule(module)

	err = drpcServer.Start(ctx)
	if err != nil {
		return errors.Wrapf(
			err, "starting socket server on %s", *unixSocket)
	}

	go func() {
		<-signals
		finish <- true
	}()
	<-finish

	return nil
}

func runDrpcClient(log logging.Logger) error {
	client := drpc.NewClientConnection(*unixSocket)

	err := client.Connect()
	if err != nil {
		return errors.Wrap(err, "connecting to socket")
	}

	message := &drpc.Call{
		Module: int32(hello.Module_HELLO),
		Method: int32(hello.Function_GREETING),
	}

	body := &hello.Hello{
		Name: "Friend",
	}
	message.Body, err = proto.Marshal(body)
	if err != nil {
		return errors.Wrap(err, "marshalling the Call body")
	}

	resp, err := client.SendMsg(message)
	if err != nil {
		return errors.Wrap(err, "sending message")
	}

	fmt.Printf("Response:")
	fmt.Printf("\tSequence: %v", resp.Sequence)
	fmt.Printf("\tStatus: %v", resp.Status)
	fmt.Printf("\tBody: %v bytes", len(resp.Body))

	respBody := &hello.HelloResponse{}
	err = proto.Unmarshal(resp.Body, respBody)
	if err != nil {
		return errors.Wrap(err, "unmarshalling HelloResponse")
	}

	log.Debugf("\tGreeting: %v", respBody.Greeting)

	log.Debugf("Done.")
	return nil
}
