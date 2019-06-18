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
	"os/user"
	"strings"

	"github.com/daos-stack/daos/src/control/log"
	"github.com/pkg/errors"
)

const msgNotExist = "No such file or directory"

// getGroupName returns group name, either of the supplied group if user
// belong to supplied group or default usr group otherwise.
func getGroupName(
	ext External, usr *user.User, tgtGroup string) (groupName string, err error) {

	var group *user.Group
	var ids []string
	groupName = usr.Username

	if tgtGroup == "" || tgtGroup != usr.Username {
		return // no useful group specified
	}

	// lookup group specified in config file
	if group, err = ext.lookupGroup(tgtGroup); err != nil {
		return
	}

	// check user group membership
	if ids, err = ext.listGroups(usr); err != nil {
		return
	}
	for _, gid := range ids {
		if group.Gid == gid {
			return tgtGroup, nil
		}
	}

	if groupName != tgtGroup {
		return "", errors.Errorf("user %s not member of group %s", usr.Username, tgtGroup)
	}

	return
}

// chownAll changes ownership of required directories (recursive) and
// files using user/group derived from config file parameters.
func chownAll(config *configuration, user string, group string) error {
	paths := []string{
		config.SocketDir,
	}

	for _, srv := range config.Servers {
		paths = append(paths, srv.ScmMount, srv.LogFile)
	}

	// add last because we don't want to fail to write before chown proc
	paths = append(paths, config.ControlLogFile)

	for _, path := range paths {
		if path == "" {
			continue
		}

		err := config.ext.runCommand(
			fmt.Sprintf("chown -R %s:%s %s", user, group, path))
		if err != nil && !strings.Contains(err.Error(), msgNotExist) {
			return err
		}
	}

	return nil
}

// changeFileOwnership changes the ownership of required files to the user and group
// specified in the server config file. Fails if specified values are invalid. User
// mandatory, group optional.
func changeFileOwnership(config *configuration) error {
	if config.UserName == "" {
		return errors.New("no username supplied in config")
	}

	log.Debugf("running as root, changing file ownership to user %s", config.UserName)

	usr, err := config.ext.lookupUser(config.UserName)
	if err != nil {
		return errors.Wrap(err, "user lookup")
	}

	groupName, err := getGroupName(config.ext, usr, config.GroupName)
	if err != nil {
		return errors.WithMessage(err, "group lookup")
	}

	if err := chownAll(config, usr.Username, groupName); err != nil {
		return err
	}

	return nil
}
