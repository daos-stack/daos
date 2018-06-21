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
// provided in Contract No. B609815.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package security

import (
	"net"
	"syscall"

	"golang.org/x/net/context"
	"golang.org/x/sys/unix"
	"google.golang.org/grpc/credentials"
)

// DomainInfo holds our socket credentials and implements the credentials.AuthInfo
// interface necessary for grpc
type DomainInfo struct {
	creds *syscall.Ucred
	ctx   string
}

// AuthType returns the type of authentication as per grpc interface
func (d *DomainInfo) AuthType() string {
	return "Ucred"
}

// DomainCreds is our custom handshake handler to extract domain socket
// credentials and store them for use in rpc handlers
type DomainCreds struct {
}

// Info is required for the TransportCredentials interface
func (d DomainCreds) Info() credentials.ProtocolInfo {
	return credentials.ProtocolInfo{
		SecurityProtocol: "domain",
		SecurityVersion:  "1.0",
		ServerName:       "localhost",
	}
}

// ClientHandshake is required for the TransportCredentials interface.
func (d *DomainCreds) ClientHandshake(_ context.Context, _ string, conn net.Conn) (_ net.Conn, _ credentials.AuthInfo, err error) {
	return conn, &DomainInfo{}, nil
}

// ServerHandshake is required for the TransportCredentials interface. It is also
// responsible for extracting the socket credentials to be stored in our DomainInfo
// structure
func (d *DomainCreds) ServerHandshake(rawConn net.Conn) (net.Conn, credentials.AuthInfo, error) {
	sock := rawConn.(*net.UnixConn)
	f, err := sock.File()
	if err != nil {
		return nil, nil, err
	}
	defer f.Close()

	fd := int(f.Fd())
	creds, err := syscall.GetsockoptUcred(fd, syscall.SOL_SOCKET, syscall.SO_PEERCRED)
	if err != nil {
		return nil, nil, err
	}
	ctx, err := unix.GetsockoptString(fd, syscall.SOL_SOCKET, syscall.SO_PEERSEC)
	if err != nil {
		return nil, nil, err
	}
	return rawConn, &DomainInfo{creds, ctx}, nil
}

// Clone is required for the TransportCredentials interface
func (d *DomainCreds) Clone() credentials.TransportCredentials {
	return NewDomainCreds()
}

// OverrideServerName is required for the TransportCredentials interface
func (d *DomainCreds) OverrideServerName(serverNameOverride string) error {
	return nil
}

// NewDomainCreds instantiates an instance of our DomainCreds
// TransportCredentials type.
func NewDomainCreds() credentials.TransportCredentials {
	dc := &DomainCreds{}
	return dc
}
