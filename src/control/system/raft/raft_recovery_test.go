//
// (C) Copyright 2022-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package raft

import (
	"context"
	"flag"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/google/uuid"
	"github.com/hashicorp/raft"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/test"
	. "github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

var regenRaftFixtures = flag.Bool("regen-raft-fixtures", false, "regenerate raft test files")

const (
	raftFixDir = "testdata/raft_recovery"
)

func waitForSnapshots(ctx context.Context, t *testing.T, log logging.Logger, cfg *DatabaseConfig, num int) {
	t.Helper()
	for {
		select {
		case <-ctx.Done():
			t.Fatal(ctx.Err())
		default:
			snaps, err := GetSnapshotInfo(log, cfg)
			if err != nil && err != ErrNoRaftSnapshots {
				t.Fatal(err)
			}
			if len(snaps) >= num {
				return
			}
			time.Sleep(1 * time.Second)
		}
	}
}

func testDbCfg() *DatabaseConfig {
	return &DatabaseConfig{
		Replicas:              []*net.TCPAddr{common.LocalhostCtrlAddr()},
		SystemName:            "daos-test",
		RaftDir:               raftFixDir,
		RaftSnapshotInterval:  1 * time.Second,
		RaftSnapshotThreshold: 8,
	}
}

func Test_Raft_RegenerateFixtures(t *testing.T) {
	if !*regenRaftFixtures {
		return
	}

	if err := os.RemoveAll(raftFixDir); err != nil {
		t.Fatal(err)
	}

	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	dbCfg := testDbCfg()
	db, err := NewDatabase(log, dbCfg)
	if err != nil {
		t.Fatal(err)
	}
	_, db.raftTransport = raft.NewInmemTransport(db.serverAddress())

	if err := createRaftDir(dbCfg.RaftDir); err != nil {
		t.Fatal(err)
	}

	ctx := test.Context(t)

	if err := db.Start(ctx); err != nil {
		t.Fatal(err)
	}
	t.Log("started database")
	waitForLeadership(ctx, t, db, true)
	t.Log("got leadership")

	maxRanks := int(dbCfg.RaftSnapshotThreshold)
	maxPools := int(dbCfg.RaftSnapshotThreshold)

	t.Log("adding ranks")
	nextAddr := ctrlAddrGen(ctx, net.IPv4(127, 0, 0, 1), 4)
	for i := 0; i < maxRanks; i++ {
		m := &system.Member{
			Rank:        NilRank,
			UUID:        uuid.New(),
			Addr:        <-nextAddr,
			State:       system.MemberStateJoined,
			FaultDomain: system.MustCreateFaultDomainFromString("/my/test/domain"),
		}

		if err := db.AddMember(m); err != nil {
			t.Fatal(err)
		}
	}
	t.Log("waiting for snapshot")
	waitForSnapshots(ctx, t, log, dbCfg, 1)

	t.Log("adding pools")
	replicas := replicaGen(ctx, maxRanks, 3)
	for i := 0; i < maxPools; i++ {
		ps := &system.PoolService{
			PoolUUID:  uuid.New(),
			PoolLabel: fmt.Sprintf("pool%04d", i),
			State:     system.PoolServiceStateReady,
			Replicas:  <-replicas,
			Storage: &system.PoolServiceStorage{
				CreationRankStr:    fmt.Sprintf("[0-%d]", maxRanks),
				PerRankTierStorage: []uint64{1, 2},
			},
		}

		lock, err := db.TakePoolLock(ctx, ps.PoolUUID)
		if err != nil {
			t.Fatal(err)
		}

		if err := db.AddPoolService(lock.InContext(ctx), ps); err != nil {
			t.Fatal(err)
		}
		lock.Release()
	}
	t.Log("waiting for snapshot")
	waitForSnapshots(ctx, t, log, dbCfg, 2)

	t.Log("stopping db")
	if err := db.Stop(); err != nil {
		t.Fatal(err)
	}
}

func Test_Raft_GetRaftConfiguration(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	dbCfg := testDbCfg()
	srcDir := dbCfg.RaftDir
	dbCfg.RaftDir = filepath.Join(t.TempDir(), filepath.Base(srcDir))
	test.CopyDir(t, srcDir, dbCfg.RaftDir)

	cfg, err := GetRaftConfiguration(log, dbCfg)
	if err != nil {
		t.Fatal(err)
	}

	expCfg := raft.Configuration{
		Servers: []raft.Server{
			{
				Address: raft.ServerAddress(dbCfg.stringReplicas()[0]),
				ID:      raft.ServerID(dbCfg.stringReplicas()[0]),
			},
		},
	}
	if diff := cmp.Diff(expCfg, cfg); diff != "" {
		t.Errorf("unexpected raft configuration (-want +got):\n%s", diff)
	}
}

func Test_Raft_RecoverLocalReplica(t *testing.T) {
	for name, tc := range map[string]struct {
		setup  func(t *testing.T) *DatabaseConfig
		expErr error
	}{
		"not a replica": {
			setup: func(t *testing.T) *DatabaseConfig {
				dbCfg := testDbCfg()
				dbCfg.RaftDir = t.TempDir()
				return dbCfg
			},
			expErr: errors.New("replica"),
		},
		"successful recovery": {
			setup: func(t *testing.T) *DatabaseConfig {
				dbCfg := testDbCfg()
				srcDir := dbCfg.RaftDir
				dbCfg.RaftDir = filepath.Join(t.TempDir(), filepath.Base(srcDir))
				test.CopyDir(t, srcDir, dbCfg.RaftDir)
				return dbCfg
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			dbCfg := tc.setup(t)
			preSnaps, err := GetSnapshotInfo(log, dbCfg)
			if err != nil && !errors.Is(err, ErrNoRaftSnapshots) {
				t.Fatal(err)
			}

			err = RecoverLocalReplica(log, dbCfg)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			postSnaps, err := GetSnapshotInfo(log, dbCfg)
			if err != nil {
				t.Fatal(err)
			}

			cmpOpts := []cmp.Option{
				cmp.Comparer(func(x, y RankSet) bool {
					return x.String() == y.String()
				}),
			}
			// A recovery should force a new snapshot to be created, so
			// the diff here should be different.
			if diff := cmp.Diff(preSnaps, postSnaps, cmpOpts...); diff == "" {
				t.Fatal("expected different snapshot info")
			}
		})
	}
}

func Test_Raft_RestoreLocalReplica(t *testing.T) {
	for name, tc := range map[string]struct {
		setup  func(t *testing.T) (*DatabaseConfig, string)
		expErr error
	}{
		"unwritable restore dir": {
			setup: func(t *testing.T) (*DatabaseConfig, string) {
				dbCfg := testDbCfg()
				srcDir := dbCfg.RaftDir
				dbCfg.RaftDir = filepath.Join(t.TempDir(), filepath.Base(srcDir))
				test.CopyDir(t, srcDir, dbCfg.RaftDir)
				if err := os.Remove(dbCfg.DBFilePath()); err != nil {
					t.Fatal(err)
				}

				restoreDir := filepath.Join(t.TempDir(), filepath.Base(srcDir))
				if err := os.Chmod(filepath.Dir(restoreDir), 0000); err != nil {
					t.Fatal(err)
				}
				return dbCfg, restoreDir
			},
			expErr: errors.New("permission denied"),
		},
		"successful restore from internal snapshot": {
			setup: func(t *testing.T) (*DatabaseConfig, string) {
				dbCfg := testDbCfg()
				srcDir := dbCfg.RaftDir
				dbCfg.RaftDir = filepath.Join(t.TempDir(), filepath.Base(srcDir))
				test.CopyDir(t, srcDir, dbCfg.RaftDir)

				return dbCfg, dbCfg.RaftDir
			},
		},
		"successful restore from external snapshot": {
			setup: func(t *testing.T) (*DatabaseConfig, string) {
				dbCfg := testDbCfg()
				srcDir := dbCfg.RaftDir
				dbCfg.RaftDir = filepath.Join(t.TempDir(), filepath.Base(srcDir))
				test.CopyDir(t, srcDir, dbCfg.RaftDir)
				if err := os.Remove(dbCfg.DBFilePath()); err != nil {
					t.Fatal(err)
				}
				return dbCfg, filepath.Join(t.TempDir(), filepath.Base(srcDir))
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			dbCfg, restoreDir := tc.setup(t)
			preSnaps, err := GetSnapshotInfo(log, dbCfg)
			if err != nil {
				t.Fatal(err)
			}

			dbCfg.RaftDir = restoreDir
			err = RestoreLocalReplica(log, dbCfg, preSnaps[0].Path)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			postSnap, err := GetLatestSnapshot(log, dbCfg)
			if err != nil {
				t.Fatal(err)
			}

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreFields(SnapshotDetails{}, "Path"),
				cmpopts.IgnoreFields(raft.SnapshotMeta{}, "ID", "Index"),
				cmp.Comparer(func(x, y RankSet) bool {
					return x.String() == y.String()
				}),
			}
			if diff := cmp.Diff(preSnaps[0], postSnap, cmpOpts...); diff != "" {
				t.Fatalf("expected post-restore snapshot info to be the same (-want +got):\n%s", diff)
			}
		})
	}
}

func Test_Raft_GetLatestSnapshot(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	dbCfg := testDbCfg()
	snapInfo, err := GetLatestSnapshot(log, dbCfg)
	if err != nil {
		t.Fatal(err)
	}

	// fixture should have two snapshots, each one generated
	// after the snapshot threshold has been reached.
	expVersion := uint(dbCfg.RaftSnapshotThreshold * 2)
	if snapInfo.Version < expVersion {
		t.Fatalf("expected snapshot version >= %d, got %d", expVersion, snapInfo.Version)
	}

	// verify that the snapshot looks sane
	expNextRank := uint(dbCfg.RaftSnapshotThreshold)
	if snapInfo.NextRank < expNextRank {
		t.Fatalf("expected next rank >= %d, got %d", expNextRank, snapInfo.NextRank)
	}
}

func Test_Raft_GetLastLogEntry(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	dbCfg := testDbCfg()
	logEntry, err := GetLastLogEntry(log, dbCfg)
	if err != nil {
		t.Fatal(err)
	}

	// fixture should have two snapshots, each one generated
	// after the snapshot threshold has been reached.
	expIndex := dbCfg.RaftSnapshotThreshold * 2
	if logEntry.Log.Index < expIndex {
		t.Fatalf("expected log Index >= %d, got %d", expIndex, logEntry.Log.Index)
	}

	test.AssertEqual(t, logEntry.Operation, raftOpAddPoolService.String(), "unexpected operation in log")
}

func Test_Raft_GetLogEntries(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	dbCfg := testDbCfg()
	logEntries, err := GetLogEntries(log, dbCfg)
	if err != nil {
		t.Fatal(err)
	}

	var logs []*LogEntryDetails
	for entry := range logEntries {
		logs = append(logs, entry)
	}

	// we expect to see at least two snapshots' worth of logs (ranks + pools)
	expIndex := dbCfg.RaftSnapshotThreshold * 2
	if len(logs) < int(expIndex) {
		t.Fatalf("expected log entries >= %d, got %d", expIndex, len(logs))
	}
}

func Test_Raft_ReadSnapshotInfo(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	latest, err := GetLatestSnapshot(log, testDbCfg())
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		setup  func(t *testing.T) string
		expErr error
	}{
		"bad snapshot path": {
			setup: func(t *testing.T) string {
				return "bad/path"
			},
			expErr: errors.New("no such file or directory"),
		},
		"missing metadata": {
			setup: func(t *testing.T) string {
				testDir := t.TempDir()
				testSnap := filepath.Join(testDir, filepath.Base(latest.Path))
				test.CopyDir(t, latest.Path, testSnap)
				if err := os.Remove(filepath.Join(testSnap, snapshotMetaFile)); err != nil {
					t.Fatal(err)
				}

				return testSnap
			},
			expErr: errors.New("no such file or directory"),
		},
		"missing data": {
			setup: func(t *testing.T) string {
				testDir := t.TempDir()
				testSnap := filepath.Join(testDir, filepath.Base(latest.Path))
				test.CopyDir(t, latest.Path, testSnap)
				if err := os.Remove(filepath.Join(testSnap, snapshotDataFile)); err != nil {
					t.Fatal(err)
				}

				return testSnap
			},
			expErr: errors.New("no such file or directory"),
		},
		"mangled metadata": {
			setup: func(t *testing.T) string {
				testDir := t.TempDir()
				testSnap := filepath.Join(testDir, filepath.Base(latest.Path))
				test.CopyDir(t, latest.Path, testSnap)
				if err := os.WriteFile(filepath.Join(testSnap, snapshotMetaFile), []byte("bad metadata"), 0644); err != nil {
					t.Fatal(err)
				}

				return testSnap
			},
			expErr: errors.New("invalid character"),
		},
		"mangled data": {
			setup: func(t *testing.T) string {
				testDir := t.TempDir()
				testSnap := filepath.Join(testDir, filepath.Base(latest.Path))
				test.CopyDir(t, latest.Path, testSnap)
				if err := os.WriteFile(filepath.Join(testSnap, snapshotDataFile), []byte("bad data"), 0644); err != nil {
					t.Fatal(err)
				}

				return testSnap
			},
			expErr: errors.New("invalid character"),
		},
		"good snapshot path": {
			setup: func(t *testing.T) string {
				testDir := t.TempDir()
				testSnap := filepath.Join(testDir, filepath.Base(latest.Path))
				test.CopyDir(t, latest.Path, testSnap)

				return testSnap
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			snapshotDir := tc.setup(t)
			snapInfo, err := ReadSnapshotInfo(snapshotDir)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreFields(SnapshotDetails{}, "Path"),
				cmp.Comparer(func(x, y RankSet) bool {
					return x.String() == y.String()
				}),
			}
			if diff := cmp.Diff(latest, snapInfo, cmpOpts...); diff != "" {
				t.Fatalf("unexpected snapshot info (-want +got):\n%s", diff)
			}
		})
	}
}
