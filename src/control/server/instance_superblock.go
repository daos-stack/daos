//
// (C) Copyright 2019-2022 Intel Corporation.
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
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

const (
	defaultStoragePath = "/mnt/daos"
	defaultGroupName   = "daos_engine"
	superblockVersion  = 1
)

// Superblock is the per-Instance superblock
type Superblock struct {
	Version         uint8
	UUID            string
	System          string
	Rank            *ranklist.Rank
	URI             string
	ValidRank       bool
	HostFaultDomain string
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

func (ei *EngineInstance) superblockPath() string {
	storagePath := ei.storage.ControlMetadataEnginePath()
	return filepath.Join(ei.fsRoot, storagePath, "superblock")
}

func (ei *EngineInstance) setSuperblock(sb *Superblock) {
	ei.Lock()
	defer ei.Unlock()
	ei._superblock = sb
}

func (ei *EngineInstance) getSuperblock() *Superblock {
	ei.RLock()
	defer ei.RUnlock()

	if ei._superblock == nil {
		return nil
	}

	// Make a read-only copy to avoid race warnings.
	// NB: There is not currently any logic that relies
	// on the returned Superblock being "live", i.e. the
	// actual in-memory struct. If that changes, then the
	// Superblock struct will probably need some locking to
	// provide thread-safe access to its fields.
	sbCopy := *ei._superblock
	return &sbCopy
}

func (ei *EngineInstance) hasSuperblock() bool {
	return ei.getSuperblock() != nil
}

// NeedsSuperblock indicates whether or not the instance appears
// to need a superblock to be created in order to start.
//
// Should not be called if SCM format is required.
func (ei *EngineInstance) NeedsSuperblock() (bool, error) {
	if ei.hasSuperblock() {
		ei.log.Debugf("instance %d has no superblock set", ei.Index())
		return false, nil
	}

	err := ei.ReadSuperblock()
	if os.IsNotExist(errors.Cause(err)) {
		ei.log.Debugf("instance %d: superblock not found", ei.Index())
		return true, nil
	}

	if err != nil {
		ei.log.Debugf("instance %d failed to read superblock", ei.Index())
		return true, errors.Wrap(err, "failed to read existing superblock")
	}

	ei.log.Debugf("instance %d: superblock found", ei.Index())
	return false, nil
}

// createSuperblock creates instance superblock if needed.
func (ei *EngineInstance) createSuperblock(recreate bool) error {
	if ei.IsStarted() {
		return errors.Errorf("can't create superblock: instance %d already started", ei.Index())
	}

	needsSuperblock, err := ei.NeedsSuperblock() // scm format completed by now
	if !needsSuperblock {
		return nil
	}
	if err != nil && !recreate {
		return err
	}

	if err := ei.MountMetadata(); err != nil {
		return err
	}

	u, err := uuid.NewRandom()
	if err != nil {
		return errors.Wrap(err, "Failed to generate instance UUID")
	}

	cfg := ei.runner.GetConfig()
	systemName := cfg.SystemName
	if systemName == "" {
		systemName = defaultGroupName
	}

	superblock := &Superblock{
		Version: superblockVersion,
		UUID:    u.String(),
		System:  systemName,
	}

	if ei.hostFaultDomain != nil {
		superblock.HostFaultDomain = ei.hostFaultDomain.String()
	}

	ei.setSuperblock(superblock)
	ei.log.Debugf("index %d: creating %s: (rank: %s, uuid: %s)",
		ei.Index(), ei.superblockPath(), superblock.Rank, superblock.UUID)

	return ei.WriteSuperblock()
}

// WriteSuperblock writes the instance's superblock
// to storage.
func (ei *EngineInstance) WriteSuperblock() error {
	ei.log.Debugf("instance %d: writing superblock at %s", ei.Index(), ei.superblockPath())
	return WriteSuperblock(ei.superblockPath(), ei.getSuperblock())
}

// ReadSuperblock reads the instance's superblock
// from storage.
func (ei *EngineInstance) ReadSuperblock() error {
	if err := ei.MountMetadata(); err != nil {
		return errors.Wrap(err, "failed to mount control metadata device")
	}

	sb, err := ReadSuperblock(ei.superblockPath())
	if err != nil {
		ei.log.Debugf("instance %d: failed to read superblock at %s: %s", ei.Index(), ei.superblockPath(), err)
		return errors.Wrap(err, "failed to read instance superblock")
	}
	ei.setSuperblock(sb)

	return nil
}

// RemoveSuperblock removes a superblock from storage.
func (ei *EngineInstance) RemoveSuperblock() error {
	ei.setSuperblock(nil)

	ei.log.Debugf("instance %d: removing superblock at %s", ei.Index(), ei.superblockPath())
	return os.Remove(ei.superblockPath())
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
