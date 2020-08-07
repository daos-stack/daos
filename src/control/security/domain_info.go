//
// (C) Copyright 2018-2020 Intel Corporation.
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

	"github.com/pkg/errors"
	"golang.org/x/sys/unix"

	"github.com/daos-stack/daos/src/control/logging"
)

// DomainInfo holds our socket credentials to be used by the DomainSocketServer
type DomainInfo struct {
	creds *syscall.Ucred
	ctx   string
}

// Uid returns the UID obtained from the domain socket
func (d *DomainInfo) Uid() uint32 {
	return d.creds.Uid
}

// Gid returns the GID obtained from the domain socket
func (d *DomainInfo) Gid() uint32 {
	return d.creds.Gid
}

// Ctx returns the additional security information obtained from the domain socket
func (d *DomainInfo) Ctx() string {
	return d.ctx
}

// InitDomainInfo returns an initialized DomainInfo structure
func InitDomainInfo(creds *syscall.Ucred, ctx string) *DomainInfo {
	return &DomainInfo{creds, ctx}
}

// DomainInfoFromUnixConn determines credentials from a unix socket.
func DomainInfoFromUnixConn(log logging.Logger, sock *net.UnixConn) (*DomainInfo, error) {
	f, err := sock.File()
	if err != nil {
		return nil, errors.Wrap(err, "Failed to get socket file")
	}
	defer f.Close()

	fd := int(f.Fd())
	creds, err := syscall.GetsockoptUcred(fd, syscall.SOL_SOCKET, syscall.SO_PEERCRED)
	if err != nil {
		return nil, errors.Wrap(err, "Failed to get sockopt creds")
	}

	ctx, err := unix.GetsockoptString(fd, syscall.SOL_SOCKET, syscall.SO_PEERSEC)
	if err != nil {
		ctx = ""
	}
	log.Debugf("client pid: %d uid: %d gid %d ctx: %s", creds.Pid, creds.Uid, creds.Gid, ctx)
	return InitDomainInfo(creds, ctx), nil
}
