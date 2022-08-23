//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/pkg/errors"
)

func subStr(in string, max int) string {
	if len(in) > max {
		return in[:max]
	}
	return in
}

func getProcPids(procDir, procName string) (pids []int, _ error) {
	allProcs, err := filepath.Glob(filepath.Join(procDir, "*", "stat"))
	if err != nil {
		return nil, errors.Wrap(err, "failed to read process list")
	}

	maxProcName := 15 // TASK_COMM_LEN-1 in linux/sched.h
	var pid, ppid, pgrp int
	for _, proc := range allProcs {
		data, err := os.ReadFile(proc)
		if err != nil {
			continue
		}
		if len(data) == 0 {
			return nil, errors.New("empty process file")
		}
		line := string(data)

		lIdx := strings.Index(line, "(")
		rIdx := strings.Index(line, ")")
		if lIdx == -1 || rIdx == -1 {
			continue
		}
		if subStr(line[lIdx+1:rIdx], maxProcName) != subStr(procName, maxProcName) {
			continue
		}

		if n, err := fmt.Sscanf(line, "%d", &pid); err != nil || n == 0 {
			return nil, errors.Wrapf(err, "failed to parse pid from process stat file %q", line)
		}
		if n, err := fmt.Sscanf(line[rIdx+3:], "%d %d", &ppid, &pgrp); err != nil || n == 0 {
			return nil, errors.Wrapf(err, "failed to parse pgrp from process stat file %q", line[rIdx+3:])
		}
		if pgrp != 0 {
			// use the process group as the pid to avoid misidentifying
			// threads as separate processes
			pids = append(pids, pgrp)
			continue
		}
		pids = append(pids, pid)
	}

	return
}

// GetProcPids returns a list of pids for the given process name.
func GetProcPids(procName string) ([]int, error) {
	return getProcPids("/proc", procName)
}

func getProcName(pid int, procDir string) (string, error) {
	exe, err := os.Readlink(filepath.Join(procDir, strconv.Itoa(pid), "exe"))
	if err != nil {
		return "", errors.Wrap(err, "failed to read executable path")
	}
	return filepath.Base(exe), nil
}

func checkDupeProcess(pid int, procDir string) error {
	name, err := getProcName(pid, procDir)
	if err != nil {
		return errors.Wrap(err, "failed to get process name")
	}
	pids, err := getProcPids(procDir, name)
	if err != nil {
		return errors.Wrap(err, "failed to read process list")
	}

	for _, otherPid := range pids {
		if otherPid == pid {
			continue
		}

		return errors.Errorf("another %s process is already running (pid: %d)", name, otherPid)
	}

	return nil
}

// CheckDupeProcess checks to see if another process with the same name as
// ours is running.
func CheckDupeProcess() error {
	return checkDupeProcess(os.Getpid(), "/proc")
}
