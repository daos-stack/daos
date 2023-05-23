//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package raft

import (
	"bytes"
	"encoding/json"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"github.com/hashicorp/raft"
	boltdb "github.com/hashicorp/raft-boltdb/v2"
	"github.com/pkg/errors"
	"go.etcd.io/bbolt"

	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	snapshotMetaFile = "meta.json"
	snapshotDataFile = "state.bin"
)

// GetRaftConfiguration returns the current raft configuration.
func GetRaftConfiguration(log logging.Logger, cfg *DatabaseConfig) (raft.Configuration, error) {
	cmps, err := ConfigureComponents(log, cfg)
	if err != nil {
		return raft.Configuration{}, errors.Wrap(err, "failed to configure raft components")
	}
	defer func() {
		if boltDB, ok := cmps.LogStore.(*boltdb.BoltStore); ok {
			boltDB.Close()
		}
	}()

	db, err := NewDatabase(log, cfg)
	if err != nil {
		return raft.Configuration{}, errors.Wrap(err, "failed to create database")
	}
	_, transport := raft.NewInmemTransport(raft.ServerAddress(db.replicaAddr.String()))

	log.Debugf("reading raft config from %s", cfg.RaftDir)
	return raft.GetConfiguration(cmps.Config, (*fsm)(db),
		cmps.LogStore, cmps.StableStore, cmps.SnapshotStore, transport)
}

// RecoverLocalReplica recovers the MS from the local on-disk replica state.
func RecoverLocalReplica(log logging.Logger, cfg *DatabaseConfig) error {
	cmps, err := ConfigureComponents(log, cfg)
	if err != nil {
		return errors.Wrap(err, "failed to configure raft components")
	}
	defer func() {
		if boltDB, ok := cmps.LogStore.(*boltdb.BoltStore); ok {
			boltDB.Close()
		}
	}()

	isReplica, err := raft.HasExistingState(cmps.LogStore, cmps.StableStore, cmps.SnapshotStore)
	if err != nil {
		return errors.Wrap(err, "failed to check for existing state")
	}
	if !isReplica {
		return &system.ErrNotReplica{Replicas: cfg.stringReplicas()}
	}

	db, err := NewDatabase(log, cfg)
	if err != nil {
		return errors.Wrap(err, "failed to create database")
	}
	_, transport := raft.NewInmemTransport(raft.ServerAddress(db.replicaAddr.String()))

	return raft.RecoverCluster(cmps.Config, (*fsm)(db),
		cmps.LogStore, cmps.StableStore, cmps.SnapshotStore,
		transport, genBootstrapCfg(db.replicaAddr))
}

func createRaftDir(dbPath string) error {
	if err := os.Mkdir(dbPath, 0700); err != nil && !os.IsExist(err) {
		return errors.Wrap(err, "failed to create raft directory")
	}
	return nil
}

func copyFile(src, dst string) error {
	st, err := os.Stat(src)
	if err != nil {
		return errors.Wrapf(err, "failed to stat %q", src)
	}

	r, err := os.Open(src)
	if err != nil {
		return errors.Wrapf(err, "failed to open %q", src)
	}
	defer r.Close()

	w, err := os.Create(dst)
	if err != nil {
		return errors.Wrapf(err, "failed to create %q", dst)
	}
	defer w.Close()

	if _, err := io.Copy(w, r); err != nil {
		return errors.Wrapf(err, "failed to copy %q to %q", src, dst)
	}

	if err := w.Sync(); err != nil {
		return errors.Wrapf(err, "failed to sync %q", dst)
	}

	if err := w.Chmod(st.Mode()); err != nil {
		return errors.Wrapf(err, "failed to chmod %q", dst)
	}

	return nil
}

func copySnapshot(src, dst string) error {
	if err := copyFile(filepath.Join(src, snapshotMetaFile), filepath.Join(dst, snapshotMetaFile)); err != nil {
		return errors.Wrap(err, "failed to copy snapshot metadata")
	}

	if err := copyFile(filepath.Join(src, snapshotDataFile), filepath.Join(dst, snapshotDataFile)); err != nil {
		return errors.Wrap(err, "failed to copy snapshot data")
	}

	return nil
}

// RestoreLocalReplica restores the MS from the snapshot at the supplied path.
func RestoreLocalReplica(log logging.Logger, cfg *DatabaseConfig, snapPath string) error {
	sInfo, err := ReadSnapshotInfo(snapPath)
	if err != nil {
		return errors.Wrapf(err, "failed to verify snapshot at %q", snapPath)
	}

	if strings.HasPrefix(snapPath, cfg.RaftDir) {
		tmpDir, err := ioutil.TempDir("", "daos-raft-restore")
		if err != nil {
			return errors.Wrap(err, "failed to create temporary directory")
		}
		defer os.RemoveAll(tmpDir)

		log.Info("Copying snapshot to intermediate temporary directory")
		if err := copySnapshot(snapPath, tmpDir); err != nil {
			return errors.Wrap(err, "failed to copy snapshot")
		}
		snapPath = tmpDir
	}

	// Nuke the existing raft directory to ensure we're restarting from a clean slate.
	log.Infof("Removing existing raft directory %s", cfg.RaftDir)
	if err := os.RemoveAll(cfg.RaftDir); err != nil {
		return errors.Wrapf(err, "failed to remove existing raft directory %q", cfg.RaftDir)
	}
	if err := createRaftDir(cfg.RaftDir); err != nil {
		return errors.Wrapf(err, "failed to recreate raft directory %q", cfg.RaftDir)
	}

	cmps, err := ConfigureComponents(log, cfg)
	if err != nil {
		return errors.Wrap(err, "failed to configure raft components")
	}
	closeBoltDB := func() error {
		if boltDB, ok := cmps.LogStore.(*boltdb.BoltStore); ok {
			return boltDB.Close()
		}
		return nil
	}
	defer func() {
		closeBoltDB()
	}()

	db, err := NewDatabase(log, cfg)
	if err != nil {
		return errors.Wrap(err, "failed to create database")
	}
	_, transport := raft.NewInmemTransport(raft.ServerAddress(db.replicaAddr.String()))

	waitLeaderReady := make(chan bool)
	cmps.Config.NotifyCh = waitLeaderReady
	svc, err := raft.NewRaft(cmps.Config, (*fsm)(db),
		cmps.LogStore, cmps.StableStore, cmps.SnapshotStore, transport)
	if err != nil {
		return errors.Wrap(err, "failed to create raft service")
	}
	defer func() {
		if err := svc.Shutdown().Error(); err != nil {
			log.Errorf("failed to shutdown raft service: %v", err)
		}
	}()

	data, err := readSnapshotData(snapPath)
	if err != nil {
		return errors.Wrapf(err, "failed to read snapshot data from %q", snapPath)
	}
	log.Info("Bootstrapping new raft service; waiting for completion")
	if f := svc.BootstrapCluster(genBootstrapCfg(db.replicaAddr)); f.Error() != nil {
		return errors.Wrap(f.Error(), "failed to bootstrap cluster")
	}

	// Wait for the leader to be ready and for the configfuration to be committed.
	<-waitLeaderReady
	for {
		stats := svc.Stats()
		if stats["commit_index"] != "0" {
			break
		}
	}

	log.Info("Leader ready; restoring snapshot")
	if err := svc.Restore(sInfo.Metadata, bytes.NewReader(data), 0); err != nil {
		return errors.Wrapf(err, "failed to restore snapshot from %q", snapPath)
	}

	log.Info("Shutting down raft service")
	if err := closeBoltDB(); err != nil {
		return errors.Wrap(err, "failed to close boltdb")
	}
	if err := svc.Shutdown().Error(); err != nil {
		log.Errorf("failed to shutdown raft service: %v", err)
	}

	// Finally, force the local replica to re-bootstrap.
	return RecoverLocalReplica(log, cfg)
}

// LogEntryDetails contains details of a raft log entry.
type LogEntryDetails struct {
	Log       raft.Log
	Time      time.Time
	Operation string
	Data      json.RawMessage
}

// DecodeLog decodes the log entry data into the LogEntryDetails.
func (led *LogEntryDetails) DecodeLog() error {
	if led == nil {
		return errors.Errorf("nil %T", led)
	}

	if len(led.Log.Data) == 0 {
		return errors.New("no log data")
	}

	type fromJSON LogEntryDetails
	from := &struct {
		Op raftOp
		*fromJSON
	}{
		fromJSON: (*fromJSON)(led),
	}

	if err := json.Unmarshal(led.Log.Data, from); err != nil {
		return err
	}
	led.Operation = from.Op.String()

	return nil
}

var (
	// ErrNoRaftLogEntries is returned when there are no raft log entries.
	ErrNoRaftLogEntries = errors.New("no raft log entries")
	// ErrNoRaftSnapshots is returned when there are no raft snapshots.
	ErrNoRaftSnapshots = errors.New("no raft snapshots")
)

// GetLogEntries returns the log entries from the raft log via a channel which
// is closed when there are no more entries to be read.
func GetLogEntries(log logging.Logger, cfg *DatabaseConfig, maxEntries ...uint64) (<-chan *LogEntryDetails, error) {
	boltOpts := boltdb.Options{
		Path: cfg.DBFilePath(),
		BoltOptions: &bbolt.Options{
			ReadOnly: true,
		},
	}
	boltDB, err := boltdb.New(boltOpts)
	if err != nil {
		return nil, errors.Wrapf(err, "failed to open boltdb at %s", cfg.DBFilePath())
	}

	li, err := boltDB.LastIndex()
	if err != nil {
		return nil, errors.Wrap(err, "failed to get last log index")
	}

	if li == 0 {
		return nil, ErrNoRaftLogEntries
	}
	minIndex := uint64(0)
	if len(maxEntries) > 0 {
		minIndex = li - maxEntries[0]
	}

	entries := make(chan *LogEntryDetails)
	go func() {
		defer boltDB.Close()

		for {
			if li <= minIndex {
				close(entries)
				return
			}

			details := new(LogEntryDetails)
			if err := boltDB.GetLog(li, &details.Log); err != nil {
				log.Errorf("failed to get log entry %d: %s", li, err)
				close(entries)
				return
			}
			log.Debugf("read log: %+v", details.Log)

			if details.Log.Type == raft.LogCommand {
				if err := details.DecodeLog(); err != nil {
					log.Errorf("failed to decode log entry %d: %s", li, err)
					close(entries)
					return
				}
			} else {
				details.Time = details.Log.AppendedAt
				details.Operation = details.Log.Type.String()
			}

			entries <- details
			li--
		}
	}()

	return entries, nil

}

// GetLastLogEntry returns the last (highest index) log entry from the raft log.
func GetLastLogEntry(log logging.Logger, cfg *DatabaseConfig) (*LogEntryDetails, error) {
	entries, err := GetLogEntries(log, cfg, 1)
	if err != nil {
		return nil, err
	}

	entry := <-entries
	if entry != nil {
		return entry, nil
	}

	return nil, ErrNoRaftLogEntries
}

// SnapshotDetails contains details of a raft snapshot, including an interpretation
// of the snapshot data in database format.
type SnapshotDetails struct {
	Metadata      *raft.SnapshotMeta
	Path          string
	Version       uint
	MapVersion    uint
	SchemaVersion uint
	NextRank      uint
	MemberRanks   *ranklist.RankSet
	Pools         []string
}

// DecodeSnapshot decodes the snapshot data into the SnapshotDetails.
func (sd *SnapshotDetails) DecodeSnapshot(data []byte) error {
	if sd == nil {
		return errors.Errorf("nil %T", sd)
	}
	if sd.Metadata == nil {
		return errors.Errorf("nil %T.Metadata", sd)
	}
	if data == nil {
		return errors.New("nil data")
	}

	type fromJSON SnapshotDetails
	from := &struct {
		Pools struct {
			Uuids map[string]*system.PoolService
		}
		Members struct {
			Uuids map[string]*system.Member
		}
		*fromJSON
	}{
		fromJSON: (*fromJSON)(sd),
	}

	if err := json.Unmarshal(data, from); err != nil {
		return err
	}

	sd.MemberRanks = ranklist.NewRankSet()
	for _, m := range from.Members.Uuids {
		sd.MemberRanks.Add(m.Rank)
	}
	for _, p := range from.Pools.Uuids {
		sd.Pools = append(sd.Pools, p.PoolLabel)
	}
	sort.Strings(sd.Pools)

	return nil
}

// GetSnapshotInfo returns a list of snapshot details found on the system.
func GetSnapshotInfo(log logging.Logger, cfg *DatabaseConfig) ([]*SnapshotDetails, error) {
	store, err := getSnapshotStore(newHcLogger(log), cfg)
	if err != nil {
		return nil, err
	}

	snaps, err := store.List()
	if err != nil {
		return nil, errors.Wrap(err, "failed to list snapshots")
	}

	if len(snaps) == 0 {
		return nil, ErrNoRaftSnapshots
	}

	details := make([]*SnapshotDetails, len(snaps))
	for i, snap := range snaps {
		snapPath := filepath.Join(cfg.RaftDir, "snapshots", snap.ID)
		details[i], err = ReadSnapshotInfo(snapPath)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to read snapshot %s", snapPath)
		}
	}

	sort.Slice(details, func(i, j int) bool {
		return details[i].Metadata.Index < details[j].Metadata.Index
	})

	return details, nil
}

// GetLatestSnapshot returns the latest snapshot found on the system.
func GetLatestSnapshot(log logging.Logger, cfg *DatabaseConfig) (*SnapshotDetails, error) {
	snaps, err := GetSnapshotInfo(log, cfg)
	if err != nil {
		return nil, err
	}

	return snaps[len(snaps)-1], nil
}

func readSnapshotMeta(path string, meta *raft.SnapshotMeta) error {
	metaPath := filepath.Join(path, snapshotMetaFile)
	data, err := ioutil.ReadFile(metaPath)
	if err != nil {
		return errors.Wrapf(err, "failed to read snapshot metadata from %q", metaPath)
	}

	return errors.Wrapf(json.Unmarshal(data, meta), "failed to parse snapshot metadata from %q", metaPath)
}

func readSnapshotData(path string) ([]byte, error) {
	dataPath := filepath.Join(path, snapshotDataFile)
	data, err := ioutil.ReadFile(dataPath)
	if err != nil {
		return nil, errors.Wrapf(err, "failed to read snapshot data from %q", dataPath)
	}

	return data, nil
}

// ReadSnapshotInfo reads the snapshot metadata and data from the given path.
func ReadSnapshotInfo(path string) (*SnapshotDetails, error) {
	details := &SnapshotDetails{
		Path:     path,
		Metadata: new(raft.SnapshotMeta),
	}
	if err := readSnapshotMeta(path, details.Metadata); err != nil {
		return nil, err
	}

	data, err := readSnapshotData(path)
	if err != nil {
		return nil, err
	}

	return details, errors.Wrapf(details.DecodeSnapshot(data), "failed to decode snapshot data in %s", path)
}
