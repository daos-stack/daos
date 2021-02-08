//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"io/ioutil"
	"os"
	"path/filepath"

	uuid "github.com/google/uuid"
	"github.com/pkg/errors"
	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	defaultStoragePath = "/mnt/daos"
	defaultGroupName   = "daos_engine"
	superblockVersion  = 1
)

// Superblock is the per-Instance superblock
type Superblock struct {
	Version   uint8
	UUID      string
	System    string
	Rank      *system.Rank
	URI       string
	ValidRank bool
}

// TODO: Marshal/Unmarshal using a binary representation?

// Marshal transforms the Superblock into a storable representation
func (sb *Superblock) Marshal() ([]byte, error) {
	data, err := yaml.Marshal(sb)
	if err != nil {
		return nil, errors.Wrapf(err, "Failed to marshal %+v", sb)
	}
	return data, nil
}

// Unmarshal reconstitutes a Superblock from a Marshaled representation
func (sb *Superblock) Unmarshal(raw []byte) error {
	return yaml.Unmarshal(raw, sb)
}

func (srv *EngineInstance) superblockPath() string {
	scmConfig := srv.scmConfig()
	storagePath := scmConfig.MountPoint
	if storagePath == "" {
		storagePath = defaultStoragePath
	}
	return filepath.Join(srv.fsRoot, storagePath, "superblock")
}

func (srv *EngineInstance) setSuperblock(sb *Superblock) {
	srv.Lock()
	defer srv.Unlock()
	srv._superblock = sb
}

func (srv *EngineInstance) getSuperblock() *Superblock {
	srv.RLock()
	defer srv.RUnlock()
	return srv._superblock
}

func (srv *EngineInstance) hasSuperblock() bool {
	return srv.getSuperblock() != nil
}

// NeedsSuperblock indicates whether or not the instance appears
// to need a superblock to be created in order to start.
//
// Should not be called if SCM format is required.
func (srv *EngineInstance) NeedsSuperblock() (bool, error) {
	if srv.hasSuperblock() {
		return false, nil
	}

	scmCfg := srv.scmConfig()

	srv.log.Debugf("%s: checking superblock", scmCfg.MountPoint)

	err := srv.ReadSuperblock()
	if os.IsNotExist(errors.Cause(err)) {
		srv.log.Debugf("%s: needs superblock (doesn't exist)", scmCfg.MountPoint)
		return true, nil
	}

	if err != nil {
		return true, errors.Wrap(err, "failed to read existing superblock")
	}

	return false, nil
}

// createSuperblock creates instance superblock if needed.
func (srv *EngineInstance) createSuperblock(recreate bool) error {
	if srv.isStarted() {
		return errors.Errorf("can't create superblock: instance %d already started", srv.Index())
	}

	needsSuperblock, err := srv.NeedsSuperblock() // scm format completed by now
	if !needsSuperblock {
		return nil
	}
	if err != nil && !recreate {
		return err
	}

	srv.log.Debugf("idx %d createSuperblock()", srv.Index())

	if err := srv.MountScmDevice(); err != nil {
		return err
	}

	u, err := uuid.NewRandom()
	if err != nil {
		return errors.Wrap(err, "Failed to generate instance UUID")
	}

	cfg := srv.runner.GetConfig()
	systemName := cfg.SystemName
	if systemName == "" {
		systemName = defaultGroupName
	}

	superblock := &Superblock{
		Version: superblockVersion,
		UUID:    u.String(),
		System:  systemName,
	}

	if cfg.Rank != nil {
		superblock.Rank = new(system.Rank)
		if cfg.Rank != nil {
			*superblock.Rank = *cfg.Rank
		}
	}
	srv.setSuperblock(superblock)
	srv.log.Debugf("creating %s: (rank: %s, uuid: %s)",
		srv.superblockPath(), superblock.Rank, superblock.UUID)

	return srv.WriteSuperblock()
}

// WriteSuperblock writes the instance's superblock
// to storage.
func (srv *EngineInstance) WriteSuperblock() error {
	return WriteSuperblock(srv.superblockPath(), srv.getSuperblock())
}

// ReadSuperblock reads the instance's superblock
// from storage.
func (srv *EngineInstance) ReadSuperblock() error {
	if err := srv.MountScmDevice(); err != nil {
		return errors.Wrap(err, "failed to mount SCM device")
	}

	sb, err := ReadSuperblock(srv.superblockPath())
	if err != nil {
		return errors.Wrap(err, "failed to read instance superblock")
	}
	srv.setSuperblock(sb)

	return nil
}

// RemoveSuperblock removes a superblock from storage.
func (srv *EngineInstance) RemoveSuperblock() error {
	srv.setSuperblock(nil)

	return os.Remove(srv.superblockPath())
}

// WriteSuperblock writes a Superblock to storage.
func WriteSuperblock(sbPath string, sb *Superblock) error {
	data, err := sb.Marshal()
	if err != nil {
		return err
	}

	return errors.Wrapf(common.WriteFileAtomic(sbPath, data, 0600),
		"Failed to write Superblock to %s", sbPath)
}

// ReadSuperblock reads a Superblock from storage.
func ReadSuperblock(sbPath string) (*Superblock, error) {
	data, err := ioutil.ReadFile(sbPath)
	if err != nil {
		return nil, errors.Wrapf(err, "Failed to read Superblock from %s", sbPath)
	}

	sb := &Superblock{}
	if err := sb.Unmarshal(data); err != nil {
		return nil, err
	}

	return sb, nil
}
