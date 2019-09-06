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

package ioserver

import (
	"os"
	"os/exec"
	"path"

	"github.com/pkg/errors"
)

type (
	cmdLogger struct {
		logFn  func(string)
		prefix string
	}
)

func (cl *cmdLogger) Write(data []byte) (int, error) {
	if cl.logFn == nil {
		return 0, errors.New("no log function set in cmdLogger")
	}

	var msg string
	if cl.prefix != "" {
		msg = cl.prefix + " "
	}
	msg += string(data)
	cl.logFn(msg)
	return len(data), nil
}

func findBinary(binName string) (string, error) {
	// Try the direct route first
	binPath, err := exec.LookPath(binName)
	if err == nil {
		return binPath, nil
	}

	// If that fails, look to see if it's adjacent to
	// this binary
	selfPath, err := os.Readlink("/proc/self/exe")
	if err != nil {
		return "", errors.Wrap(err, "unable to determine path to self")
	}
	binPath = path.Join(path.Dir(selfPath), binName)
	if _, err := os.Stat(binPath); err != nil {
		return "", errors.Errorf("unable to locate %s", binName)
	}
	return binPath, nil
}
