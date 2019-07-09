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

package server

import (
	"os/user"
	"strconv"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/log"
)

func getUID(ext External, userName string) (*user.User, int64, error) {
	usr, err := ext.lookupUser(userName)
	if err != nil {
		return nil, -1, errors.WithMessage(err, "user lookup")
	}

	uid, err := strconv.ParseInt(usr.Uid, 10, 32)
	if err != nil {
		return nil, -1, errors.WithMessage(err, "parsing uid to int")
	}

	return usr, uid, nil
}

func getGID(ext External, usr *user.User, groupName string) (int64, error) {
	_gid := usr.Gid

	if groupName != "" {
		// attempt to assign group specified in config file
		if group, err := ext.lookupGroup(groupName); err == nil {
			// check user group membership
			if ids, err := ext.listGroups(usr); err == nil {
				for _, g := range ids {
					if group.Gid == g {
						_gid = g
						break
					}
				}

				if _gid != group.Gid {
					log.Debugf(
						"user %s not member of group %s",
						usr.Username, group.Name)
				}
			} else {
				return -1, errors.WithMessage(
					err, "get group membership")
			}
		} else {
			log.Debugf("group lookup: %+v", err)
		}
	}

	gid, err := strconv.ParseInt(_gid, 10, 32)
	if err != nil {
		return -1, errors.WithMessage(err, "parsing gid to int")
	}

	return gid, nil
}

func chownScmMount(config *configuration, uid int64, gid int64) error {
	for i, srv := range config.Servers {
		// chown ScmMount (uid/gid are 32bit)
		err := config.ext.chown(srv.ScmMount, int(uid), int(gid))
		if err != nil {
			return errors.WithMessagef(
				err,
				"changing scm mount ownership"+
					" on I/O server %d", i)
		}
	}
	return nil
}

// drop will attempt to drop privileges by setting uid of running process to
// that of the username specified in config file. If groupname is also
// specified in config file then check user is a member of that group and
// set relevant gid if so, otherwise use user.gid.
func dropPrivileges(config *configuration) error {
	if config.UserName == "" {
		return errors.New("no username supplied in config")
	}

	log.Debugf("running as root, downgrading to user %s", config.UserName)

	usr, uid, err := getUID(config.ext, config.UserName)
	if err != nil {
		return errors.WithMessage(err, "get uid")
	}

	gid, err := getGID(config.ext, usr, config.GroupName)
	if err != nil {
		return errors.WithMessage(err, "get gid")
	}

	if err := chownScmMount(config, uid, gid); err != nil {
		return err
	}

	if err := config.ext.setGID(gid); err != nil {
		return errors.WithMessage(err, "setting gid")
	}

	if err := config.ext.setUID(uid); err != nil {
		return errors.WithMessage(err, "setting uid")
	}

	// TODO: chown sock path?

	return nil
}
