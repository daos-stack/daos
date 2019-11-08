//
// (C) Copyright 2018-2019 Intel Corporation.
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
	"os/exec"
	"sort"
	"strings"

	"github.com/daos-stack/daos/src/control/client"
	"github.com/daos-stack/daos/src/control/logging"
)

const (
	cmdNodesetExpand = "nodeset -e --separator=',' %s"
	cmdNodesetFold   = "nodeset -f --separator=',' %s"
	cmdClubak        = "clubak -b < %s" // normally supply filename
)

type runCmdFn func(string) (string, error)
type lookPathFn func(string) (string, error)

type runCmdError struct {
	wrapped error
	stdout  string
}

func (rce *runCmdError) Error() string {
	if ee, ok := rce.wrapped.(*exec.ExitError); ok {
		return fmt.Sprintf("%s: stdout: %s; stderr: %s", ee.ProcessState,
			rce.stdout, ee.Stderr)
	}

	return fmt.Sprintf("%s: stdout: %s", rce.wrapped.Error(), rce.stdout)
}

func run(cmd string) (string, error) {
	out, err := exec.Command("bash", "-c", cmd).Output()
	if err != nil {
		return "", &runCmdError{
			wrapped: err,
			stdout:  string(out),
		}
	}

	return string(out), nil
}

type cmdRunner struct {
	log      logging.Logger
	runCmd   runCmdFn
	lookPath lookPathFn
}

func defaultCmdRunner(log logging.Logger) *cmdRunner {
	return &cmdRunner{log: log, runCmd: run, lookPath: exec.LookPath}
}

// checkNodeset verifies nodeset application is installed (part of clustershell).
func (r *cmdRunner) checkNodeset() error {
	_, err := r.lookPath("nodeset")
	if err != nil {
		return FaultMissingNodeset
	}

	return nil
}

// expandHosts takes string specifying host pattern and returns slice of host addresses.
func (r *cmdRunner) expandHosts(hostPattern string) []string {
	hosts := strings.Split(hostPattern, ",")

	// check to see if we can use nodeset to expand hostlist pattern
	if err := r.checkNodeset(); err != nil {
		r.log.Debug(err.Error())
		return hosts // fallback
	}

	out, err := r.runCmd(fmt.Sprintf(cmdNodesetExpand,
		strings.TrimSpace(hostPattern)))
	if err != nil {
		r.log.Debug(err.Error())
		return hosts // fallback
	}

	return strings.Split(out, ",")
}

// foldHosts takes a slice of addresses and returns a folded pattern.
func (r *cmdRunner) foldHosts(hosts []string) string {
	hostsStr := strings.TrimSpace(strings.Join(hosts, ","))

	// check to see if we can use nodeset to fold slice of addresses
	if err := r.checkNodeset(); err != nil {
		r.log.Debug(err.Error())
		return hostsStr // fallback
	}

	out, err := r.runCmd(fmt.Sprintf(cmdNodesetFold, hostsStr))
	if err != nil {
		r.log.Debug(err.Error())
		return hostsStr // fallback
	}

	return out
}

func (r *cmdRunner) hasConns(results client.ResultMap) (bool, string) {
	out := r.sprintConns(results)
	for _, res := range results {
		if res.Err == nil {
			return true, out
		}
	}

	// notify if there have been no successful connections
	return false, fmt.Sprintf("%sNo active connections!\n", out)
}

func (r *cmdRunner) sprintConns(results client.ResultMap) (out string) {
	var active []string
	var inactive []string

	// map keys always processed in order
	for addr := range results {
		active = append(active, addr)
	}
	sort.Strings(active)

	i := 0
	for _, addr := range active {
		if results[addr].Err != nil {
			r.log.Debugf("failed to connect to %s (%s)",
				addr, results[addr].Err)
			inactive = append(inactive, addr)
			continue
		}
		active[i] = addr
		i++
	}
	active = active[:i]

	// attempt to fold lists of addresses
	if len(inactive) > 0 {
		out = fmt.Sprintf("%s Inactive\n",
			strings.TrimSpace(r.foldHosts(inactive)))
	}
	if len(active) > 0 {
		out = fmt.Sprintf("%s%s Active\n", out,
			strings.TrimSpace(r.foldHosts(active)))
	}

	return out
}

func (r *cmdRunner) aggregate(in string) (out string, err error) {
	fName := "/tmp/storage_summary.tmp"

	if err = r.checkNodeset(); err != nil {
		return
	}

	if err = ioutil.WriteFile(fName, []byte(in), 0644); err != nil {
		return
	}

	return r.runCmd(fmt.Sprintf(cmdClubak, fName))
}
