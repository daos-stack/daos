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
	"net"
	"syscall"

	"google.golang.org/grpc"

	"security"
)

var (
	serverAddr         = flag.String("server_addr", "127.0.0.1:10000", "The server address in the format of host:port")
	serverHostOverride = flag.String("server_host_override", "", "The server name use to verify the hostname returned by TLS handshake")
	grpcSocket         = flag.String("grpc_socket", "/tmp/agent/daos_agent.grpc", "The path to the unix socket to be used for receiving local messages")
)

func main() {
	flag.Parse()

	// Setup our grpc channel with a simple insecure connection
	var opts []grpc.DialOption
	opts = append(opts, grpc.WithInsecure())
	conn, err := grpc.Dial(*serverAddr, opts...)
	if err != nil {
		log.Fatalf("fail to dial: %v", err)
	}
	defer conn.Close()

	// We need to ensure the socket file does not exist otherwise
	// listen will fail.
	syscall.Unlink(*grpcSocket)
	lis, err := net.Listen("unix", *grpcSocket)
	if err != nil {
		log.Fatalf("Unable to listen on unix socket %s: %v", *grpcSocket, err)
	}
	// We defer an unlink of the socket for the case where the agent terminates
	// properly.
	defer syscall.Unlink(*grpcSocket)

	var serverOpts []grpc.ServerOption
	// Use our custom DomainCredential object instead of the standard one.
	serverOpts = append(serverOpts, grpc.Creds(security.NewDomainCreds()))
	grpcServer := grpc.NewServer(serverOpts...)
	// Nothing to chat with the server about for the moment
	grpcServer.Serve(lis)

}
