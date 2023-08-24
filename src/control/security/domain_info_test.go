//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security_test

import (
	"fmt"
	"syscall"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/security"
)

func TestSecurity_DomainInfo_String(t *testing.T) {
	pid1Str := "systemd"
	sysDistro := system.GetDistribution()
	if sysDistro.ID == "debian" {
		pid1Str = "init"
	}

	ucred_noPid := &syscall.Ucred{
		Pid: 0,
		Uid: 123456,
		Gid: 789012,
	}
	ucred_Pid := &syscall.Ucred{
		Pid: 1, // should be systemd on any modern system
		Uid: 0,
		Gid: 0,
	}
	for name, tc := range map[string]struct {
		di     *security.DomainInfo
		expStr string
	}{
		"nil": {
			di:     nil,
			expStr: "nil",
		},
		"empty": {
			di:     &security.DomainInfo{},
			expStr: "nil creds",
		},
		"nil creds": {
			di:     security.InitDomainInfo(nil, "ctx"),
			expStr: "nil creds",
		},
		"creds (no PID)": {
			di:     security.InitDomainInfo(ucred_noPid, "ctx"),
			expStr: "pid: 0 uid: 123456 gid: 789012",
		},
		"creds (PID)": {
			di:     security.InitDomainInfo(ucred_Pid, "ctx"),
			expStr: fmt.Sprintf("pid: 1 (%s) uid: 0 (root) gid: 0 (root)", pid1Str),
		},
	} {
		t.Run(name, func(t *testing.T) {
			if diff := cmp.Diff(tc.expStr, tc.di.String()); diff != "" {
				t.Fatalf("unexpected DomainInfo string (-want, +got):\n%s", diff)
			}
		})
	}
}
