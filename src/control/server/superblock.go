package main

import (
	"io/ioutil"
	"path/filepath"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/pkg/errors"
	uuid "github.com/satori/go.uuid"
	"gopkg.in/yaml.v2"
)

const (
	defaultStoragePath = "/mnt/daos"
	defaultGroupName   = "daos_io_server"
	superblockVersion  = 0
)

// Superblock is the per-Instance superblock
type Superblock struct {
	Version uint8
	UUID    string
	System  string
	Rank    string
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

func (mi *managedInstance) superblockPath() string {
	storagePath, err := mi.cfg.Get("storagePath")
	if err != nil {
		storagePath = defaultStoragePath
	}
	return filepath.Join(mi.fsRoot, storagePath, "superblock")
}

// CreateSuperblock creates the superblock for this Instance
func (mi *managedInstance) CreateSuperblock() error {
	u, err := uuid.NewV4()
	if err != nil {
		return errors.Wrap(err, "Failed to generate instance UUID")
	}

	systemName, err := mi.cfg.Get("serverGroup")
	if err != nil {
		systemName = defaultGroupName
	}
	rank, err := mi.cfg.Get("serverRank")
	if err != nil {
		rank = nilRank.String()
	}

	mi.superblock = &Superblock{
		Version: superblockVersion,
		UUID:    u.String(),
		System:  systemName,
		Rank:    rank,
	}

	return mi.WriteSuperblock(mi.superblock)
}

// WriteSuperblock writes the server's Superblock
func (mi *managedInstance) WriteSuperblock(sb *Superblock) error {
	sbPath := mi.superblockPath()
	data, err := sb.Marshal()
	if err != nil {
		return err
	}

	return errors.Wrapf(common.WriteFileAtomic(sbPath, data, 0600),
		"Failed to write Superblock to %s", sbPath)
}

// ReadSuperblock attempts to read the server's Superblock
func (mi *managedInstance) ReadSuperblock() (*Superblock, error) {
	sbPath := mi.superblockPath()
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
