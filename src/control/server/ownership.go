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
	"bytes"
	"fmt"
	"os"
	"os/exec"
	"os/user"
	"strconv"

	"github.com/daos-stack/daos/src/control/log"
	"github.com/pkg/errors"
)

// getGroup returns group name, either of the supplied group if user
// belong to supplied group or default usr group otherwise.
func getGroup(
	ext External, usr *user.User, tgtGroup string) (group *user.Group, err error) {

	var ids []string

	if tgtGroup == "" || tgtGroup == usr.Username {
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
			return group, nil
		}
	}

	return group, errors.Errorf("user %s not member of group %s", usr.Username, tgtGroup)
}

// chownAll changes ownership of required directories (recursive) and
// files using user/group derived from config file parameters.
func chownAll(config *configuration, usr *user.User, grp *user.Group) error {
	uid, err := strconv.ParseInt(usr.Uid, 10, 32)
	if err != nil {
		return errors.Wrap(err, "parsing uid to int")
	}

	gidS := usr.Gid
	if grp != nil {
		gidS = grp.Gid // valid group
	}

	gid, err := strconv.ParseInt(gidS, 10, 32)
	if err != nil {
		return errors.Wrap(err, "parsing gid to int")
	}

	log.Debugf("running as root, changing file ownership to uid/gid %d/%d", uid, gid)

	paths := []string{
		config.SocketDir,
		config.ControlLogFile,
	}

	for _, srv := range config.Servers {
		paths = append(paths, srv.ScmMount, srv.LogFile)
	}

	for _, path := range paths {
		if path == "" {
			continue
		}

		err := config.ext.chownR(path, int(uid), int(gid)) // 32 bit ints from ParseInt
		if err != nil && !os.IsNotExist(err) {
			return errors.Wrapf(err, "recursive chown %s", path)
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

	usr, err := config.ext.lookupUser(config.UserName)
	if err != nil {
		return errors.Wrap(err, "user lookup")
	}

	grp, err := getGroup(config.ext, usr, config.GroupName)
	if err != nil {
		return errors.WithMessage(err, "group lookup")
	}

	if err := chownAll(config, usr, grp); err != nil {
		return err
	}

	return nil
}

// respawnProc builds and executes command chain to respawn process under new ownership.
func respawnProc(config *configuration) error {
	var buf bytes.Buffer // build command string

	// Wait for this proc to exit and make NVMe storage accessible to new user.
	fmt.Fprintf(
		&buf, `sleep 1 && %s storage prep-nvme -u %s &> %s`,
		os.Args[0], config.UserName, config.ControlLogFile)

	// Run daos_server from within a subshell of target user with the same args.
	fmt.Fprintf(&buf, ` && su %s -c "`, config.UserName)

	for _, arg := range os.Args {
		fmt.Fprintf(&buf, arg+" ")
	}

	// Redirect output of new proc to existing log file.
	fmt.Fprintf(&buf, `&> %s"`, config.ControlLogFile)

	msg := fmt.Sprintf("dropping privileges: re-spawning (%s)\n", buf.String())
	log.Debugf(msg)

	if err := exec.Command("bash", "-c", buf.String()).Start(); err != nil {
		return err
	}

	return nil
}
