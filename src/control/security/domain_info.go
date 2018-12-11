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

package security

import (
	"net"
	"syscall"

	"github.com/daos-stack/daos/src/control/log"
	"golang.org/x/sys/unix"
)

// DomainInfo holds our socket credentials to be used by the DomainSocketServer
type DomainInfo struct {
	creds *syscall.Ucred
	ctx   string
}

// InitDomainInfo returns an initialized DomainInfo structure
func InitDomainInfo(creds *syscall.Ucred, ctx string) *DomainInfo {
	return &DomainInfo{creds, ctx}
}

// DomainInfoFromUnixConn determines credentials from a unix socket.
func DomainInfoFromUnixConn(sock *net.UnixConn) (*DomainInfo, error) {
	f, err := sock.File()
	if err != nil {
		return nil, err
	}
	defer f.Close()

	log.Debugf("Client Socket Credentials:\n")
	fd := int(f.Fd())
	creds, err := syscall.GetsockoptUcred(fd, syscall.SOL_SOCKET, syscall.SO_PEERCRED)
	if err != nil {
		return nil, err
	}
	log.Debugf("Pid: %d\n", creds.Pid)
	log.Debugf("Uid: %d\n", creds.Uid)
	log.Debugf("Gid: %d", creds.Gid)

	ctx, err := unix.GetsockoptString(fd, syscall.SOL_SOCKET, syscall.SO_PEERSEC)
	if err != nil {
		log.Debugf("Unable to obtain peer context: %s\n", err)
		ctx = ""
	}
	log.Debugf("Context: %s", ctx)
	return InitDomainInfo(creds, ctx), nil
}
