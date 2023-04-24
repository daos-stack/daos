// Package transport provides a Transport for github.com/hashicorp/raft over gRPC.
package transport

import (
	"sync"

	pb "github.com/Jille/raft-grpc-transport/proto"
	"github.com/hashicorp/raft"
	"google.golang.org/grpc"
)

type Manager struct {
	localAddress raft.ServerAddress
	dialOptions  []grpc.DialOption

	rpcChan          chan raft.RPC
	heartbeatFunc    func(raft.RPC)
	heartbeatFuncMtx sync.Mutex

	connectionsMtx sync.Mutex
	connections    map[raft.ServerID]*conn
}

// New creates both components of raft-grpc-transport: a gRPC service and a Raft Transport.
func New(localAddress raft.ServerAddress, dialOptions []grpc.DialOption) *Manager {
	return &Manager{
		localAddress: localAddress,
		dialOptions:  dialOptions,

		rpcChan:     make(chan raft.RPC),
		connections: map[raft.ServerID]*conn{},
	}
}

// Register the RaftTransport gRPC service on a gRPC server.
func (m *Manager) Register(s *grpc.Server) {
	pb.RegisterRaftTransportServer(s, gRPCAPI{manager: m})
}

// Transport returns a raft.Transport that communicates over gRPC.
func (m *Manager) Transport() raft.Transport {
	return raftAPI{m}
}
