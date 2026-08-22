// Package transport provides a Transport for github.com/hashicorp/raft over gRPC.
package transport

import (
	"sync"
	"time"

	pb "github.com/Jille/raft-grpc-transport/proto"
	"github.com/hashicorp/go-multierror"
	"github.com/hashicorp/raft"
	"github.com/pkg/errors"
	"google.golang.org/grpc"
)

var (
	errCloseErr = errors.New("error closing connections")
)

type Manager struct {
	localAddress raft.ServerAddress
	dialOptions  []grpc.DialOption

	rpcChan          chan raft.RPC
	heartbeatFunc    func(raft.RPC)
	heartbeatFuncMtx sync.Mutex
	heartbeatTimeout time.Duration

	connectionsMtx sync.Mutex
	connections    map[raft.ServerAddress]*conn

	shutdown     bool
	shutdownCh   chan struct{}
	shutdownLock sync.Mutex
}

// New creates both components of raft-grpc-transport: a gRPC service and a Raft Transport.
func New(localAddress raft.ServerAddress, dialOptions []grpc.DialOption, options ...Option) *Manager {
	m := &Manager{
		localAddress: localAddress,
		dialOptions:  dialOptions,

		rpcChan:     make(chan raft.RPC),
		connections: map[raft.ServerAddress]*conn{},

		shutdownCh: make(chan struct{}),
	}
	for _, opt := range options {
		opt(m)
	}
	return m
}

// Register the RaftTransport gRPC service on a gRPC server.
func (m *Manager) Register(s grpc.ServiceRegistrar) {
	pb.RegisterRaftTransportServer(s, gRPCAPI{manager: m})
}

// Transport returns a raft.Transport that communicates over gRPC.
func (m *Manager) Transport() raft.Transport {
	return raftAPI{m}
}

func (m *Manager) Close() error {
	m.shutdownLock.Lock()
	defer m.shutdownLock.Unlock()

	if m.shutdown {
		return nil
	}

	close(m.shutdownCh)
	m.shutdown = true
	return m.disconnectAll()
}

func (m *Manager) disconnectAll() error {
	m.connectionsMtx.Lock()
	defer m.connectionsMtx.Unlock()

	err := errCloseErr
	for k, conn := range m.connections {
		// Lock conn.mtx to ensure Dial() is complete
		conn.mtx.Lock()
		conn.mtx.Unlock()
		closeErr := conn.clientConn.Close()
		if closeErr != nil {
			err = multierror.Append(err, closeErr)
		}
		delete(m.connections, k)
	}

	if err != errCloseErr {
		return err
	}

	return nil
}
