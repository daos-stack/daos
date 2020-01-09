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
	"os"
	"path/filepath"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

var daosVersion string

func exitWithError(log logging.Logger, err error) {
	if err == nil {
		err = errors.New("Unknown error")
	}
	log.Error(err.Error())
	os.Exit(1)
}

func configureLogging(binName string) logging.Logger {
	logLevel := logging.LogLevelError
	combinedOut := ioutil.Discard
	if logPath, set := os.LookupEnv(pbin.DaosAdminLogFileEnvVar); set {
		lf, err := common.AppendFile(logPath)
		if err == nil {
			combinedOut = lf
			logLevel = logging.LogLevelDebug
		}
	}

	// By default, we only want to log errors to stderr.
	return logging.NewCombinedLogger(binName, combinedOut).
		WithErrorLogger(logging.NewCommandLineErrorLogger(os.Stderr)).
		WithLogLevel(logLevel)
}

func checkParentName(log logging.Logger) {
	pPath, err := os.Readlink(fmt.Sprintf("/proc/%d/exe", os.Getppid()))
	if err != nil {
		exitWithError(log, errors.Wrap(err, "failed to check parent process binary"))
	}
	daosServer := "daos_server"
	if !strings.HasSuffix(pPath, daosServer) {
		exitWithError(log, errors.Errorf("%s (version %s) may only be invoked by %s",
			os.Args[0], daosVersion, daosServer))
	}
}

func main() {
	binName := filepath.Base(os.Args[0])
	log := configureLogging(binName)

	checkParentName(log)

	if os.Geteuid() != 0 {
		exitWithError(log, errors.Errorf("%s not setuid root", binName))
	}

	// hack for stuff that doesn't use geteuid() (e.g. ipmctl)
	if err := setuid(0); err != nil {
		exitWithError(log, errors.Wrap(err, "unable to setuid(0)"))
	}

	conn := pbin.NewStdioConn(binName, "daos_server", os.Stdin, os.Stdout)
	req, err := readRequest(log, conn)
	if err != nil {
		exitWithError(log, err)
	}

	scmProvider := scm.DefaultProvider(log).WithForwardingDisabled()
	bdevProvider := bdev.DefaultProvider(log).WithForwardingDisabled()
	if err := handleRequest(log, scmProvider, bdevProvider, req, conn); err != nil {
		exitWithError(log, err)
	}
}
