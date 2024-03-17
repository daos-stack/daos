//
// (C) Copyright 2018-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package drpc

import (
	"context"
	"net"
	"os"
	"sync"
	"syscall"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
)

// MaxMsgSize is the maximum drpc message size that may be sent.
// Using a packetsocket over the unix domain socket means that we receive
// a whole message at a time without knowing its size. So for this reason
// we need to restrict the maximum message size so we can preallocate a
// buffer to put all of the information in. Corresponding C definition is
// found in include/daos/drpc.h
const MaxMsgSize = 1 << 17

// DomainSocketServer is the object that listens for incoming dRPC connections,
// maintains the connections for sessions, and manages the message processing.
type DomainSocketServer struct {
	log           logging.Logger
	sockFile      string
	sockFileMode  os.FileMode
	listener      net.Listener
	service       *ModuleService
	sessions      map[net.Conn]*Session
	sessionsMutex sync.Mutex
}

// closeSession cleans up the session and removes it from the list of active
// sessions.
func (d *DomainSocketServer) closeSession(s *Session) {
	d.sessionsMutex.Lock()
	s.Close()
	delete(d.sessions, s.Conn)
	d.sessionsMutex.Unlock()
}

// listenSession runs the listening loop for a Session. It listens for incoming
// dRPC calls and processes them.
func (d *DomainSocketServer) listenSession(ctx context.Context, s *Session) {
	for {
		if err := s.ProcessIncomingMessage(ctx); err != nil {
			d.closeSession(s)
			break
		}
	}
}

// Listen listens for incoming connections on the UNIX domain socket and
// creates individual sessions for each one.
func (d *DomainSocketServer) Listen(ctx context.Context) {
	go func() {
		<-ctx.Done()
		d.log.Debug("Quitting listener")
		d.listener.Close()
	}()

	for {
		conn, err := d.listener.Accept()
		if err != nil {
			// If we're shutting down anyhow, don't print connection errors.
			if ctx.Err() == nil {
				d.log.Errorf("%s: failed to accept connection: %v", d.sockFile, err)
			}
			return
		}

		c := NewSession(conn, d.service)
		d.sessionsMutex.Lock()
		d.sessions[conn] = c
		d.sessionsMutex.Unlock()
		go d.listenSession(ctx, c)
	}
}

// Start sets up the dRPC server socket and kicks off the listener goroutine.
func (d *DomainSocketServer) Start(ctx context.Context) error {
	if d == nil {
		return errors.New("DomainSocketServer is nil")
	}

	addr := &net.UnixAddr{Name: d.sockFile, Net: "unixpacket"}
	if err := d.checkExistingSocket(ctx, addr); err != nil {
		return err
	}

	lis, err := net.ListenUnix("unixpacket", addr)
	if err != nil {
		return errors.Wrapf(err, "unable to listen on unix socket %s", d.sockFile)
	}
	d.listener = lis

	if err := os.Chmod(d.sockFile, d.sockFileMode); err != nil {
		return errors.Wrapf(err, "unable to set permissions on %s", d.sockFile)
	}

	go d.Listen(ctx)
	return nil
}

func (d *DomainSocketServer) checkExistingSocket(ctx context.Context, addr *net.UnixAddr) error {
	conn, err := net.DialUnix("unixpacket", nil, addr)
	if err == nil {
		_ = conn.Close()
		return FaultSocketFileInUse(d.sockFile)
	}

	if errors.Is(err, syscall.ENOENT) {
		return nil
	}

	if errors.Is(err, syscall.ECONNREFUSED) {
		// File exists but no one is listening - it's safe to delete.
		if err := syscall.Unlink(addr.Name); err != nil && !os.IsNotExist(err) {
			return errors.Wrap(err, "unlink old socket file")
		}
		return nil
	}

	return err
}

// RegisterRPCModule takes a Module and associates it with the given
// DomainSocketServer so it can be used to process incoming dRPC calls.
func (d *DomainSocketServer) RegisterRPCModule(mod Module) {
	d.service.RegisterModule(mod)
}

// NewDomainSocketServer returns a new unstarted instance of a
// DomainSocketServer for the specified unix domain socket path.
func NewDomainSocketServer(log logging.Logger, sock string, sockMode os.FileMode) (*DomainSocketServer, error) {
	if sock == "" {
		return nil, errors.New("Missing Argument: sockFile")
	}
	if sockMode == 0 {
		return nil, errors.New("Missing Argument: sockFileMode")
	}
	service := NewModuleService(log)
	sessions := make(map[net.Conn]*Session)
	return &DomainSocketServer{
		log:          log,
		sockFile:     sock,
		sockFileMode: sockMode,
		service:      service,
		sessions:     sessions}, nil
}

// Session represents an individual client connection to the Domain Socket Server.
type Session struct {
	Conn net.Conn
	mod  *ModuleService
}

// ProcessIncomingMessage listens for an incoming message on the session,
// calls its handler, and sends the response.
func (s *Session) ProcessIncomingMessage(ctx context.Context) error {
	buffer := make([]byte, MaxMsgSize)

	bytesRead, err := s.Conn.Read(buffer)
	if err != nil {
		// This indicates that we have reached a bad state
		// for the connection and we need to terminate the handler.
		return err
	}

	response, err := s.mod.ProcessMessage(ctx, s, buffer[:bytesRead])
	if err != nil {
		// The only way we hit here is if we fail to marshal the module's
		// response. Should not actually be possible. ProcessMessage
		// will generate a valid Response structure for any bad input.
		return err
	}

	_, err = s.Conn.Write(response)
	if err != nil {
		// This should only happen if we're shutting down while
		// trying to send our response.
		return err
	}

	return nil
}

// Close closes the session
func (s *Session) Close() {
	_ = s.Conn.Close()
}

// NewSession creates a new dRPC Session object
func NewSession(conn net.Conn, svc *ModuleService) *Session {
	return &Session{
		Conn: conn,
		mod:  svc,
	}
}
