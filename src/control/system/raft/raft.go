//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package raft

import (
	"encoding/json"
	"io"
	"net"
	"os"
	"time"

	transport "github.com/Jille/raft-grpc-transport"
	"github.com/hashicorp/go-hclog"
	"github.com/hashicorp/raft"
	boltdb "github.com/hashicorp/raft-boltdb/v2"
	"github.com/pkg/errors"
	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

// This file contains the "guts" of the new MS database. The basic theory
// is that our MS database can be modeled as a FSM, with every modification
// captured by a discrete log entry. These logs are distributed to a set of
// replicas, and a given entry is marked committed when a quorum of replicas
// have indicated that it has been persisted.
//
// https://github.com/hashicorp/raft
// https://raft.github.io/
// https://www.hashicorp.com/resources/raft-consul-consensus-protocol-explained

const (
	raftOpAddMember raftOp = iota + 1
	raftOpUpdateMember
	raftOpRemoveMember
	raftOpAddPoolService
	raftOpUpdatePoolService
	raftOpRemovePoolService
	raftOpIncMapVer
	raftOpUpdateSystemAttrs

	sysDBFile = "daos_system.db"
)

type (
	raftOp uint32

	// raftUpdate provides some metadata for an update operation.
	// The data is an opaque blob to raft.
	raftUpdate struct {
		Time time.Time
		Op   raftOp
		Data json.RawMessage
	}

	// memberUpdate provides some metadata for a membership update. In
	// particular, it specifies whether or not the NextRank counter should
	// be incremented in order for the next new member to receive a unique rank.
	memberUpdate struct {
		Member   *system.Member
		NextRank bool
	}
)

func (ro raftOp) String() string {
	return [...]string{
		"noop",
		"addMember",
		"updateMember",
		"removeMember",
		"addPoolService",
		"updatePoolService",
		"removePoolService",
		"incMapVer",
		"updateSystemAttrs",
	}[ro]
}

// IsRaftLeadershipError returns true if the given error is a known
// leadership error returned by the raft library.
func IsRaftLeadershipError(err error) bool {
	switch errors.Cause(err) {
	case raft.ErrLeadershipLost, raft.ErrLeadershipTransferInProgress,
		raft.ErrNotLeader:
		return true
	default:
		return false
	}
}

// ResignLeadership causes this instance to give up its raft
// leadership state. No-op if there is only one replica configured.
func (db *Database) ResignLeadership(cause error) error {
	if cause == nil {
		cause = errors.New("unknown error")
	}
	db.log.Errorf("resigning leadership (%s)", cause)
	if err := db.raft.withReadLock(func(svc raftService) error {
		return svc.LeadershipTransfer().Error()
	}); err != nil {
		return errors.Wrap(err, cause.Error())
	}
	return cause
}

// ShutdownRaft signals that the raft implementation should shut down
// and release any resources it is holding. Blocks until the shutdown
// is complete.
func (db *Database) ShutdownRaft() error {
	db.log.Debug("shutting down raft instance")
	return db.raft.withReadLock(func(svc raftService) error {
		// Call the raft implementation's shutdown and block
		// until it completes.
		shutdownErr := svc.Shutdown().Error()

		// Try to run all of the defined shutdown callbacks. Logging
		// any failures is the best we can do, as we really want to
		// run as many of them as possible in order to clean things
		// up.
		if shutdownErr == nil {
			for _, cb := range db.onRaftShutdown {
				if cbErr := cb(); cbErr != nil {
					db.log.Errorf("onRaftShutdown callback failed: %s", cbErr)
				}
			}
		}

		return shutdownErr
	})
}

const (
	// numRetainSnapshots is the number of snapshots to retain in the
	// snapshot store.
	numRetainSnapshots = 2
)

func getSnapshotStore(log hclog.Logger, dbCfg *DatabaseConfig) (raft.SnapshotStore, error) {
	// Check to see if the database directory exists before we configure the store.
	var dbDirExists bool
	if _, err := os.Stat(dbCfg.RaftDir); err == nil {
		dbDirExists = true
	}

	store, err := raft.NewFileSnapshotStoreWithLogger(dbCfg.RaftDir, numRetainSnapshots, log)
	if err != nil {
		return nil, err
	}

	// If the directory did not exist before we configured the store, then
	// we want to remove it in order to avoid interfering with other
	// initialization logic. This is a kludge, but it's the best we
	// can do given the current raft implementation.
	if !dbDirExists {
		if err := os.RemoveAll(dbCfg.RaftDir); err != nil {
			return nil, errors.Wrapf(err, "failed to clean up %s", dbCfg.RaftDir)
		}
	}

	return store, nil
}

// ConfigureComponents configures the raft components of the database.
func ConfigureComponents(log logging.Logger, dbCfg *DatabaseConfig) (*RaftComponents, error) {
	if _, err := os.Stat(dbCfg.RaftDir); err != nil {
		return nil, errors.Wrapf(err, "raft directory %s is not accessible", dbCfg.RaftDir)
	}

	repAddr, err := dbCfg.LocalReplicaAddr()
	if err != nil {
		return nil, err
	}

	// Start with the default raft config and modify as necessary.
	raftCfg := raft.DefaultConfig()
	raftCfg.Logger = newHcLogger(log)
	// The default threshold is 8192, ehich is way too high for
	// this use case. Our MS DB shouldn't be particularly high
	// volume, so set this value to strike a balance between
	// creating snapshots too frequently and not often enough.
	raftCfg.SnapshotThreshold = 32
	raftCfg.HeartbeatTimeout = 2000 * time.Millisecond
	raftCfg.ElectionTimeout = 2000 * time.Millisecond
	raftCfg.LeaderLeaseTimeout = 1000 * time.Millisecond
	// Set the local ID to the address of the replica.
	raftCfg.LocalID = raft.ServerID(repAddr.String())

	snaps, err := getSnapshotStore(raftCfg.Logger, dbCfg)
	if err != nil {
		return nil, errors.Wrap(err, "failed to init snapshot store")
	}

	boltDB, err := boltdb.NewBoltStore(dbCfg.DBFilePath())
	if err != nil {
		return nil, errors.Wrapf(err, "failed to init boltdb at %s", dbCfg.DBFilePath())
	}

	return &RaftComponents{
		Logger:        log,
		Config:        raftCfg,
		LogStore:      boltDB,
		StableStore:   boltDB,
		SnapshotStore: snaps,
	}, nil
}

// ConfigureTransport configures the raft transport for the database.
func (db *Database) ConfigureTransport(srv *grpc.Server, dialOpts ...grpc.DialOption) error {
	repAddr, err := db.cfg.LocalReplicaAddr()
	if err != nil {
		// no-op if the system is not configured as a MS replica.
		if system.IsNotReplica(err) {
			return nil
		}
		return err
	}

	tm := transport.New(raft.ServerAddress(repAddr.String()), dialOpts)
	tm.Register(srv)
	db.raftTransport = &loggingTransport{
		Transport: tm.Transport(),
		log:       db.log,
	}

	return nil
}

// initRaft sets up the backing raft service for use. If the service has
// already been bootstrapped, then it will start immediately. Otherwise,
// it will need to be bootstrapped before it can be used.
func (db *Database) initRaft() error {
	if err := createRaftDir(db.cfg.RaftDir); err != nil {
		return err
	}

	cmps, err := ConfigureComponents(db.log, db.cfg)
	if err != nil {
		return errors.Wrap(err, "failed to configure raft components")
	}

	// NB: ConfigureTransport() should have been called by now.
	if db.raftTransport == nil {
		return errors.New("no raft transport configured")
	}

	// Rank 0 is reserved for the first instance on the bootstrap server.
	// NB: This is a bit of a hack. It would be better to persist this
	// as a log entry, but there isn't a safe way to guarantee that it's
	// the first log applied to the bootstrap instance to be replicated
	// to peers as they're added. Instead, we just set everyone to start
	// at rank 1 and increment from there as memberUpdate logs are applied.
	db.data.NextRank = 1

	// Set the channel to be used for monitoring leadership changes.
	cmps.Config.NotifyCh = db.raftLeaderNotifyCh
	// Set a closure to properly close the boltDB store when the raft
	// instance is shut down.
	if boltDB, ok := cmps.StableStore.(*boltdb.BoltStore); ok {
		db.OnRaftShutdown(func() error {
			return boltDB.Close()
		})
	}

	r, err := raft.NewRaft(
		cmps.Config,        // *raft.Config
		(*fsm)(db),         // raft.FSM
		cmps.LogStore,      // raft.LogStore
		cmps.StableStore,   // raft.StableStore
		cmps.SnapshotStore, // raft.SnapshotStore
		db.raftTransport,   // raft.Transport
	)
	if err != nil {
		return err
	}
	db.raft.setSvc(r)
	db.initialized.SetTrue()

	return nil
}

func genBootstrapCfg(localReplicaAddr *net.TCPAddr) raft.Configuration {
	return raft.Configuration{
		Servers: []raft.Server{
			{
				Suffrage: raft.Voter,
				ID:       raft.ServerID(localReplicaAddr.String()),
				Address:  raft.ServerAddress(localReplicaAddr.String()),
			},
		},
	}
}

// bootstrapRaft handles the initial bootstrap of the backing raft instance.
// It is a no-op if the replica is already bootstrapped or is not designated
// as the bootstrap server.
func (db *Database) bootstrapRaft(newDB bool) error {
	db.log.Debugf("isBootstrap: %t, newDB: %t", db.IsBootstrap(), newDB)

	if db.IsBootstrap() && newDB {
		db.log.Debugf("bootstrapping MS on %s", db.replicaAddr)
		if err := db.raft.withReadLock(func(svc raftService) error {
			if f := svc.BootstrapCluster(genBootstrapCfg(db.replicaAddr)); f.Error() != nil {
				return errors.Wrapf(f.Error(), "failed to bootstrap raft instance on %s", db.replicaAddr)
			}
			return nil
		}); err != nil {
			return err
		}
	}

	return nil
}

// loggingTransport is used to wrap a raft.Transport with methods that
// log the action and arguments. Used for debugging while the feature is
// being developed.
type loggingTransport struct {
	raft.Transport
	log logging.Logger
}

/*
func (dt *loggingTransport) AppendEntriesPipeline(id raft.ServerID, target raft.ServerAddress) (raft.AppendPipeline, error) {
	dt.log.Debugf("ApppendEntriesPipeline(%s, %s)", id, target)
	return dt.Transport.AppendEntriesPipeline(id, target)
}

func (dt *loggingTransport) AppendEntries(id raft.ServerID, target raft.ServerAddress, args *raft.AppendEntriesRequest, resp *raft.AppendEntriesResponse) error {
	dt.log.Debugf("ApppendEntries(%s, %s) req: %+v", id, target, args)
	return dt.Transport.AppendEntries(id, target, args, resp)
}
*/

func (dt *loggingTransport) RequestVote(id raft.ServerID, target raft.ServerAddress, args *raft.RequestVoteRequest, resp *raft.RequestVoteResponse) error {
	dt.log.Debugf("RequestVote(%s, %s) req: %+v", id, target, args)
	return dt.Transport.RequestVote(id, target, args, resp)
}

func (dt *loggingTransport) InstallSnapshot(id raft.ServerID, target raft.ServerAddress, args *raft.InstallSnapshotRequest, resp *raft.InstallSnapshotResponse, data io.Reader) error {
	dt.log.Debugf("InstallSnapshot(%s, %s) req: %+v", id, target, args)
	return dt.Transport.InstallSnapshot(id, target, args, resp, data)
}

func (dt *loggingTransport) TimeoutNow(id raft.ServerID, target raft.ServerAddress, args *raft.TimeoutNowRequest, resp *raft.TimeoutNowResponse) error {
	dt.log.Debugf("TimeoutNow(%s, %s) req: %+v", id, target, args)
	return dt.Transport.TimeoutNow(id, target, args, resp)
}

// serverAddress returns a raft.ServerAddress representation of
// the db's replica address.
func (db *Database) serverAddress() raft.ServerAddress {
	return raft.ServerAddress(db.replicaAddr.String())
}

// createRaftUpdate serializes the inner payload and then wraps
// it with a *raftUpdate that is submitted to the raft service.
func createRaftUpdate(op raftOp, inner interface{}) ([]byte, error) {
	data, err := json.Marshal(inner)
	if err != nil {
		return nil, err
	}
	return json.Marshal(&raftUpdate{
		Time: time.Now(),
		Op:   op,
		Data: data,
	})
}

// submitMapVerInc submits the map version increment operation to the raft service.
func (db *Database) submitIncMapVer() error {
	data, err := createRaftUpdate(raftOpIncMapVer, nil)
	if err != nil {
		return err
	}
	return db.submitRaftUpdate(data)
}

// submitMemberUpdate submits the given member update operation to
// the raft service.
func (db *Database) submitMemberUpdate(op raftOp, m *memberUpdate) error {
	m.Member.LastUpdate = time.Now()
	data, err := createRaftUpdate(op, m)
	if err != nil {
		return err
	}
	db.log.Debugf("member %d:%x updated @ %s", m.Member.Rank, m.Member.Incarnation, m.Member.LastUpdate)
	return db.submitRaftUpdate(data)
}

// submitPoolUpdate submits the given pool service update operation to
// the raft service.
func (db *Database) submitPoolUpdate(op raftOp, ps *system.PoolService) error {
	ps.LastUpdate = time.Now()
	data, err := createRaftUpdate(op, ps)
	if err != nil {
		return err
	}
	db.log.Debugf("pool %s updated @ %s", ps.PoolUUID, ps.LastUpdate)
	return db.submitRaftUpdate(data)
}

// submitSystemAttrsUpdate submits the given system properties update
// the raft service.
func (db *Database) submitSystemAttrsUpdate(props map[string]string) error {
	data, err := createRaftUpdate(raftOpUpdateSystemAttrs, props)
	if err != nil {
		return err
	}
	return db.submitRaftUpdate(data)
}

// submitRaftUpdate submits the serialized operation to the raft service.
func (db *Database) submitRaftUpdate(data []byte) error {
	return db.raft.withReadLock(func(svc raftService) error {
		err := svc.Apply(data, 0).Error()

		// In the case that leadership is lost while trying to
		// apply an update, return a sentinel error that may
		// signal some callers to retry the operation on the
		// new leader.
		if IsRaftLeadershipError(err) {
			return system.ErrRaftUnavail
		}

		return err
	})
}

// Everything above here happens on the current leader.
// ----------------------------------------------------
// Everything below here happens on N replicas.

// NB: This type alias allows us to use a Database object as a raft.FSM.
type fsm Database

// EmergencyShutdown is called when a FSM Apply fails or in some other
// situation where the only safe response is to immediately shut down
// the raft instance and prevent it from participating in the cluster.
// After a shutdown, the control plane server must be restarted in order
// to bring this node back into the raft cluster.
func (f *fsm) EmergencyShutdown(err error) {
	f.log.Errorf("EMERGENCY RAFT SHUTDOWN due to %s", err)
	_ = f.raft.withReadLock(func(svc raftService) error {
		// Call .Error() on the future returned from
		// raft.Shutdown() in order to block on completion.
		// The error returned from this future is always nil.
		return svc.Shutdown().Error()
	})
}

// Apply is called after the log entry has been committed. This is the
// only place that direct modification of the data should occur.
func (f *fsm) Apply(l *raft.Log) interface{} {
	c := new(raftUpdate)
	if err := json.Unmarshal(l.Data, c); err != nil {
		f.EmergencyShutdown(errors.Wrapf(err, "failed to unmarshal %+v", l.Data))
		return nil
	}

	switch c.Op {
	case raftOpIncMapVer:
		f.data.applyMapVersionIncrement()
	case raftOpAddMember, raftOpUpdateMember, raftOpRemoveMember:
		f.data.applyMemberUpdate(c.Op, c.Data, f.EmergencyShutdown)
	case raftOpAddPoolService, raftOpUpdatePoolService, raftOpRemovePoolService:
		f.data.applyPoolUpdate(c.Op, c.Data, f.EmergencyShutdown)
	case raftOpUpdateSystemAttrs:
		f.data.applySystemUpdate(c.Op, c.Data, f.EmergencyShutdown)
	default:
		f.EmergencyShutdown(errors.Errorf("unhandled Apply operation: %d", c.Op))
		return nil
	}

	f.data.Lock()
	f.data.Version++ // Successful updates should increment this value.
	f.data.Unlock()

	return nil
}

// applyMapVersionIncrement is responsible for incrementing the group map version.
func (d *dbData) applyMapVersionIncrement() {
	d.Lock()
	defer d.Unlock()
	d.MapVersion++
}

// applyMemberUpdate is responsible for applying the membership update
// operation to the database.
func (d *dbData) applyMemberUpdate(op raftOp, data []byte, panicFn func(error)) {
	m := new(memberUpdate)
	if err := json.Unmarshal(data, m); err != nil {
		panicFn(errors.Wrap(err, "failed to decode member update"))
		return
	}

	d.Lock()
	defer d.Unlock()

	switch op {
	case raftOpAddMember:
		d.Members.addMember(m.Member)
	case raftOpUpdateMember:
		d.Members.updateMember(m.Member)
	case raftOpRemoveMember:
		d.Members.removeMember(m.Member)
	default:
		panicFn(errors.Errorf("unhandled Member Apply operation: %d", op))
		return
	}

	if m.NextRank {
		d.NextRank++
	}
	d.MapVersion++
}

// applyPoolUpdate is responsible for applying the pool service update
// operation to the database.
func (d *dbData) applyPoolUpdate(op raftOp, data []byte, panicFn func(error)) {
	ps := new(system.PoolService)
	if err := json.Unmarshal(data, ps); err != nil {
		panicFn(errors.Wrap(err, "failed to decode pool service update"))
		return
	}

	d.Lock()
	defer d.Unlock()

	switch op {
	case raftOpAddPoolService:
		d.Pools.addService(ps)
	case raftOpUpdatePoolService:
		cur, found := d.Pools.Uuids[ps.PoolUUID]
		if !found {
			panicFn(errors.Errorf("pool service update for unknown pool %+v", ps))
			return
		}
		d.Pools.updateService(cur, ps)
	case raftOpRemovePoolService:
		d.Pools.removeService(ps)
	default:
		panicFn(errors.Errorf("unhandled Pool Service Apply operation: %d", op))
		return
	}
}

// applySystemUpdate is responsible for applying the system properties update
// operation to the database.
func (d *dbData) applySystemUpdate(op raftOp, data []byte, panicFn func(error)) {
	props := make(map[string]string)
	if err := json.Unmarshal(data, &props); err != nil {
		panicFn(errors.Wrap(err, "failed to decode system properties update"))
		return
	}

	d.Lock()
	defer d.Unlock()

	switch op {
	case raftOpUpdateSystemAttrs:
		for k, v := range props {
			if v == "" {
				delete(d.System.Attributes, k)
				continue
			}
			d.System.Attributes[k] = v
		}
	default:
		panicFn(errors.Errorf("unhandled System Apply operation: %d", op))
		return
	}
}

// Snapshot is called to support log compaction, so that we don't have to keep
// every log entry from the start of the system. Instead, the raft service periodically
// creates a point-in-time snapshot which can be used to restore the current state, or
// to efficiently catch up a peer.
func (f *fsm) Snapshot() (raft.FSMSnapshot, error) {
	f.data.Lock()
	defer f.data.Unlock()

	data, err := json.Marshal(f.data)
	if err != nil {
		return nil, err
	}

	f.log.Debugf("created raft db snapshot (map version %d; data version %d)", f.data.MapVersion, f.data.Version)
	return &fsmSnapshot{data}, nil
}

// Restore is called to force the FSM to read in a snapshot, discarding any previous state.
func (f *fsm) Restore(rc io.ReadCloser) error {
	db, _ := NewDatabase(nil, nil)
	if err := json.NewDecoder(rc).Decode(db.data); err != nil {
		return err
	}

	if db.data.SchemaVersion != CurrentSchemaVersion {
		return errors.Errorf("restored schema version %d != %d",
			db.data.SchemaVersion, CurrentSchemaVersion)
	}

	f.data.Lock()
	f.data.Members = db.data.Members
	f.data.Pools = db.data.Pools
	f.data.NextRank = db.data.NextRank
	f.data.MapVersion = db.data.MapVersion
	f.data.System = db.data.System
	f.data.Version = db.data.Version
	f.data.Unlock()
	f.log.Debugf("db snapshot loaded (map version %d; data version %d)", db.data.MapVersion, db.data.Version)
	return nil
}

// fsmSnapshot implements the raft.FSMSnapshot interface, and is used
// to persist the snapshot to an io.WriteCloser.
type fsmSnapshot struct {
	data []byte
}

// Persist writes the snapshot to the supplied raft.SnapshotSink.
func (f *fsmSnapshot) Persist(sink raft.SnapshotSink) error {
	err := func() error {
		if _, err := sink.Write(f.data); err != nil {
			return err
		}

		return sink.Close()
	}()

	if err != nil {
		_ = sink.Cancel()
	}

	return err
}

// Release is a no-op for this implementation.
func (f *fsmSnapshot) Release() {}
