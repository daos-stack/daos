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
	"os"
	"path/filepath"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

func exitWithError(log logging.Logger, err error) {
	if err == nil {
		err = errors.New("Unknown error")
	}
	log.Errorf("ERROR: %s", err)
	os.Exit(1)
}

func main() {
	binName := filepath.Base(os.Args[0])
	// send all log output to stderr for now
	// FIXME: only send errors to stderr; send debug logging to file?
	log := logging.NewCombinedLogger(binName, os.Stderr).
		WithLogLevel(logging.LogLevelDebug)

	lf, err := common.AppendFile("/tmp/daos_admin.log")
	if err != nil {
		exitWithError(log, err)
	}
	log.WithErrorLogger(logging.NewErrorLogger("admin", lf)).
		WithInfoLogger(logging.NewInfoLogger("admin", lf)).
		WithDebugLogger(logging.NewDebugLogger(lf))

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
	if err := handleRequest(log, scmProvider, req, conn); err != nil {
		exitWithError(log, err)
	}
}
