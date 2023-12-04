//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"io"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/system"
	sdb "github.com/daos-stack/daos/src/control/system/raft"
)

type msCmdRoot struct {
	Status  msStatusCmd   `command:"status" description:"Show status of the local management service replica"`
	Recover msRecoveryCmd `command:"recover" description:"Recover the management service using this replica"`
	Restore msRestoreCmd  `command:"restore" description:"Restore the management service from a snapshot"`
}

type dbCfgCmd struct {
	cfgCmd
	cmdutil.LogCmd
}

func (cmd *dbCfgCmd) getDatabaseConfig() (*sdb.DatabaseConfig, error) {
	if err := cmd.config.Validate(cmd.Logger); err != nil {
		return nil, err
	}

	dbCfg, err := server.CreateDatabaseConfig(cmd.config)
	if err != nil {
		if system.IsNotReplica(err) {
			return nil, errors.Errorf("this node is not a %s replica", build.ManagementServiceName)
		}
		return nil, errors.Wrap(err, "failed to create database config")
	}
	return dbCfg, nil
}

func printSnapshotDetails(out io.Writer, sInfo *sdb.SnapshotDetails) error {
	ew := txtfmt.NewErrWriter(out)

	fmt.Fprintf(ew, "Path: %s\n", sInfo.Path)
	fmt.Fprintf(ew, "Index: %d\n", sInfo.Metadata.Index)
	fmt.Fprintf(ew, "Term: %d\n", sInfo.Metadata.Term)
	fmt.Fprintf(ew, "DB Version: %d\n", sInfo.Version)
	fmt.Fprintf(ew, "System Ranks: %s\n", sInfo.MemberRanks)
	fmt.Fprintf(ew, "System Pools: %s\n", strings.Join(sInfo.Pools, ","))

	return ew.Err
}

func printLogEntryDetails(out io.Writer, entry *sdb.LogEntryDetails) error {
	ew := txtfmt.NewErrWriter(out)

	fmt.Fprintf(ew, "Index: %d\n", entry.Log.Index)
	fmt.Fprintf(ew, "Term: %d\n", entry.Log.Term)
	fmt.Fprintf(ew, "Time: %s\n", entry.Time)
	fmt.Fprintf(ew, "Op: %s\n", entry.Operation)
	fmt.Fprintf(ew, "Data: %s\n", entry.Data)

	return ew.Err
}

type msStatusCmd struct {
	dbCfgCmd
}

func (cmd *msStatusCmd) Execute([]string) error {
	if err := common.CheckDupeProcess(); err != nil {
		return err
	}

	dbCfg, err := cmd.getDatabaseConfig()
	if err != nil {
		return err
	}

	raftCfg, err := sdb.GetRaftConfiguration(cmd.Logger, dbCfg)
	if err != nil {
		return errors.Wrap(err, "failed to get raft configuration")
	}

	repAddr, err := dbCfg.LocalReplicaAddr()
	if err != nil {
		return errors.Wrap(err, "failed to get local replica address")
	}

	var buf strings.Builder
	fmt.Fprintf(&buf, "%s DB status:\n", build.ManagementServiceName)
	fmt.Fprintln(&buf, "  Control configuration:")
	fmt.Fprintf(&buf, "    Local replica address: %s\n", repAddr)
	peers := dbCfg.PeerReplicaAddrs()
	if len(peers) > 0 {
		peerStrs := func() (strs []string) {
			for _, peer := range peers {
				strs = append(strs, peer.String())
			}
			return
		}()
		fmt.Fprintf(&buf, "    Peer replica addresses: %s\n", strings.Join(peerStrs, ","))
	}
	fmt.Fprintln(&buf, "  Raft configuration (servers):")
	for _, srv := range raftCfg.Servers {
		status := "peer"
		if string(srv.Address) == repAddr.String() {
			status = "local"
		}
		fmt.Fprintf(&buf, "    %s: %s\n", srv.Address, status)
	}

	entry, err := sdb.GetLastLogEntry(cmd.Logger, dbCfg)
	if err == nil {
		fmt.Fprintln(&buf, "  Latest committed log entry:")
		printLogEntryDetails(txtfmt.NewIndentWriter(&buf, txtfmt.WithPadCount(4)), entry)
	} else if errors.Is(err, sdb.ErrNoRaftLogEntries) {
		fmt.Fprintln(&buf, "  No log entries found")
	} else {
		return errors.Wrap(err, "failed to get last log entry")
	}

	sInfo, err := sdb.GetLatestSnapshot(cmd.Logger, dbCfg)
	if err == nil {
		fmt.Fprintln(&buf, "  Snapshot info:")
		printSnapshotDetails(txtfmt.NewIndentWriter(&buf, txtfmt.WithPadCount(4)), sInfo)
	} else if errors.Is(err, sdb.ErrNoRaftSnapshots) {
		fmt.Fprintln(&buf, "  No snapshot info found")
	} else {
		return errors.Wrap(err, "failed to get snapshot info")
	}

	cmd.Info(buf.String())

	return nil
}

type msRecoveryCmd struct {
	dbCfgCmd

	Force bool `short:"f" long:"force" description:"Don't prompt for confirmation"`
}

func (cmd *msRecoveryCmd) Execute([]string) error {
	if err := common.CheckDupeProcess(); err != nil {
		return err
	}

	dbCfg, err := cmd.getDatabaseConfig()
	if err != nil {
		return err
	}

	msg := `
Running this command will recover the management service using this replica.
Any outstanding logs will be committed and a snapshot will be made before
updating the raft configuration to force this node to be re-bootstrapped
into single-node mode. Peer replicas will re-join the quorum as they
are restarted.

WARNING: Any uncommitted logs on the peer replicas will be discarded
in favor of the data on this replica.

Requirements:
  - This node must have already been a replica
  - All other control plane servers must be stopped

After successful completion of this command, the control plane service
may be started normally across the system. This node will serve as the
initial leader and will send the latest snapshot to other replicas as
they join the quorum.

`
	if !cmd.Force {
		cmd.Info(msg)
		if !common.GetConsent(cmd.Logger) {
			return nil
		}
	}

	if err := sdb.RecoverLocalReplica(cmd.Logger, dbCfg); err != nil {
		return err
	}

	sInfo, err := sdb.GetLatestSnapshot(cmd.Logger, dbCfg)
	if err != nil {
		return errors.Wrap(err, "failed to get latest snapshot after recovery")
	}

	cmd.Info("Successfully recovered management service")

	var buf strings.Builder
	fmt.Fprintln(&buf, "Latest snapshot info:")
	printSnapshotDetails(txtfmt.NewIndentWriter(&buf, txtfmt.WithPadCount(2)), sInfo)
	cmd.Info(buf.String())

	return nil
}

type msRestoreCmd struct {
	dbCfgCmd

	Force bool   `short:"f" long:"force" description:"Don't prompt for confirmation"`
	Path  string `short:"p" long:"path" description:"Path to snapshot file" required:"1"`
}

func (cmd *msRestoreCmd) Execute([]string) error {
	if err := common.CheckDupeProcess(); err != nil {
		return err
	}

	dbCfg, err := cmd.getDatabaseConfig()
	if err != nil {
		return err
	}

	msg := `
Running this command will restore the management service to this replica.
Upon successful restoration from snapshot, the raft configuration will be
updated to force this node to be re-bootstrapped into single-node mode.
Peer replicas will re-join the quorum as they are restarted.

WARNING: This is a potentially-destructive operation. Any local data on this
replica will be replaced by the snapshot data, and any uncommitted logs
on peer replicas will be discarded in favor of the data on this replica.

Requirements:
  - Valid snapshot file must be available
  - All other control plane servers must be stopped

After successful completion of this command, the control plane service
may be started normally across the system. This node will serve as the
initial leader and will send the latest snapshot to other replicas as
they join the quorum.

`

	sInfo, err := sdb.ReadSnapshotInfo(cmd.Path)
	if err != nil {
		return errors.Wrapf(err, "failed to read snapshot file %q", cmd.Path)
	}

	if !cmd.Force {
		cmd.Info(msg)

		var buf strings.Builder
		fmt.Fprintf(&buf, "Snapshot info read from %s:\n", cmd.Path)
		printSnapshotDetails(txtfmt.NewIndentWriter(&buf, txtfmt.WithPadCount(2)), sInfo)
		cmd.Infof("%s\n", buf.String())

		if !common.GetConsent(cmd.Logger) {
			return nil
		}
	}

	if err := sdb.RestoreLocalReplica(cmd.Logger, dbCfg, cmd.Path); err != nil {
		return err
	}

	sInfo, err = sdb.GetLatestSnapshot(cmd.Logger, dbCfg)
	if err != nil {
		return errors.Wrap(err, "failed to get latest snapshot after restoration")
	}

	cmd.Info("Successfully restored management service")

	var buf strings.Builder
	fmt.Fprintln(&buf, "Latest snapshot info:")
	printSnapshotDetails(txtfmt.NewIndentWriter(&buf, txtfmt.WithPadCount(2)), sInfo)
	cmd.Info(buf.String())

	return nil
}
