//
// (C) Copyright 2019 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package main

import (
	"fmt"
	"io/ioutil"
	"net"
	"os"
	"path/filepath"

	"github.com/pkg/errors"
	"github.com/satori/go.uuid"
	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/log"
)

func formatIosrvs(config *configuration, reformat bool) error {
	// Determine if an I/O server needs to createMS or bootstrapMS.
	addr, err := net.ResolveTCPAddr("tcp", fmt.Sprintf("0.0.0.0:%d", config.Port))
	if err != nil {
		return errors.WithStack(err)
	}
	createMS, bootstrapMS, err := checkMgmtSvcReplica(addr, config.AccessPoints)
	if err != nil {
		return err
	}

	for i := range config.Servers {
		// Only the first I/O server can be an MS replica.
		if i == 0 {
			err = formatIosrv(config, i, reformat, createMS, bootstrapMS)
		} else {
			err = formatIosrv(config, i, reformat, false, false)
		}
		if err != nil {
			return err
		}
	}

	return nil
}

func formatIosrv(config *configuration, i int, reformat, createMS, bootstrapMS bool) error {
	op := "format"
	if reformat {
		op = "reformat"
	}
	op += " server " + config.Servers[i].ScmMount

	if _, err := os.Stat(iosrvSuperPath(config.Servers[i].ScmMount)); err == nil {
		if reformat {
			return errors.New(op + ": reformat not implemented yet")
		}
		return nil
	} else if !os.IsNotExist(err) {
		return errors.Wrap(err, op)
	}

	log.Debugf(op+" (createMS=%t bootstrapMS=%t)", createMS, bootstrapMS)

	if err := createIosrvSuper(config, i, reformat, createMS, bootstrapMS); err != nil {
		return errors.WithMessage(err, op)
	}

	return nil
}

// iosrvSuper is the per-I/O-server "superblock".
type iosrvSuper struct {
	UUID        string
	System      string
	Rank        *rank
	CreateMS    bool
	BootstrapMS bool
}

// iosrvSuperPath returns the path to the I/O server superblock file.
func iosrvSuperPath(root string) string {
	return filepath.Join(root, "superblock")
}

// createIosrvSuper creates the superblock file for config.Servers[i]. Called
// when formatting an I/O server.
func createIosrvSuper(config *configuration, i int, reformat, createMS, bootstrapMS bool) error {
	// Initialize the superblock object.
	u, err := uuid.NewV4()
	if err != nil {
		return errors.Wrap(err, "generate server UUID")
	}
	super := &iosrvSuper{
		UUID:        u.String(),
		System:      config.SystemName,
		CreateMS:    createMS,
		BootstrapMS: bootstrapMS,
	}
	if config.Servers[i].Rank != nil {
		super.Rank = new(rank)
		*super.Rank = *config.Servers[i].Rank
	}

	// Write the superblock.
	return writeIosrvSuper(iosrvSuperPath(config.Servers[i].ScmMount), super)
}

func readIosrvSuper(path string) (*iosrvSuper, error) {
	b, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, errors.WithStack(err)
	}

	var s iosrvSuper
	if err = yaml.Unmarshal(b, &s); err != nil {
		return nil, errors.Wrapf(err, "unmarshal %s", path)
	}

	return &s, nil
}

func writeIosrvSuper(path string, super *iosrvSuper) error {
	data, err := yaml.Marshal(super)
	if err != nil {
		return errors.Wrapf(err, "marshal %s", path)
	}

	return common.WriteFileAtomic(path, data, 0600)
}
