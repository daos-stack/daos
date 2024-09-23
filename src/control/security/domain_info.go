//
// (C) Copyright 2018-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"fmt"
	"net"
	"os/user"
	"syscall"

	"github.com/pkg/errors"
	"golang.org/x/sys/unix"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

// DomainInfo holds our socket credentials to be used by the DomainSocketServer
type DomainInfo struct {
	creds *syscall.Ucred
	ctx   string
}

func getUserName(uid uint32) (string, error) {
	u, err := user.LookupId(fmt.Sprintf("%d", uid))
	if err != nil {
		return "", errors.Wrap(err, "Failed to lookup user")
	}
	return u.Username, nil
}

func getGroupName(gid uint32) (string, error) {
	g, err := user.LookupGroupId(fmt.Sprintf("%d", gid))
	if err != nil {
		return "", errors.Wrap(err, "Failed to lookup group")
	}
	return g.Name, nil
}

func (d *DomainInfo) String() string {
	if d == nil {
		return "nil"
	}
	if d.creds == nil {
		return "nil creds"
	}
	outStr := fmt.Sprintf("pid: %d", d.creds.Pid)
	if pName, err := common.GetProcName(int(d.creds.Pid)); err == nil {
		outStr += fmt.Sprintf(" (%s)", pName)
	}
	outStr += fmt.Sprintf(" uid: %d", d.creds.Uid)
	if uName, err := getUserName(d.creds.Uid); err == nil {
		outStr += fmt.Sprintf(" (%s)", uName)
	}
	outStr += fmt.Sprintf(" gid: %d", d.creds.Gid)
	if gName, err := getGroupName(d.creds.Gid); err == nil {
		outStr += fmt.Sprintf(" (%s)", gName)
	}
	return outStr
}

// Pid returns the PID obtained from the domain socket
func (d *DomainInfo) Pid() int32 {
	return d.creds.Pid
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
	return InitDomainInfo(creds, ctx), nil
}
