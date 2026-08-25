//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"path/filepath"
	"testing"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	sectest "github.com/daos-stack/daos/src/control/security/test"
)

func TestDmg_validityFlag(t *testing.T) {
	for name, tc := range map[string]struct {
		in     string
		exp    time.Duration
		expErr bool
	}{
		"days":      {in: "90d", exp: 90 * 24 * time.Hour},
		"weeks":     {in: "26w", exp: 26 * 7 * 24 * time.Hour},
		"years":     {in: "2y", exp: 2 * 365 * 24 * time.Hour},
		"no unit":   {in: "90", expErr: true},
		"bad unit":  {in: "90h", expErr: true},
		"zero":      {in: "0d", expErr: true},
		"negative":  {in: "-1d", expErr: true},
		"empty":     {in: "", expErr: true},
		"not a num": {in: "xd", expErr: true},
	} {
		t.Run(name, func(t *testing.T) {
			var f validityFlag
			err := f.UnmarshalFlag(tc.in)
			if tc.expErr {
				if err == nil {
					t.Fatalf("expected error for %q", tc.in)
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if f.Duration != tc.exp {
				t.Fatalf("got %s, want %s", f.Duration, tc.exp)
			}
		})
	}
}

// A generated pool CA key pair is deleted only when the CA is known not to
// be installed; otherwise the pool would require certs nobody can issue.
func TestDmg_PoolNodeAuthEnable_KeyPairSurvivesInstallFailure(t *testing.T) {
	poolUUID := test.MockUUID()
	caResp := &mgmtpb.PoolGetCAResp{PoolUuid: poolUUID}
	addErr := errors.New("evicting existing handles failed")

	for name, tc := range map[string]struct {
		addCA     *control.UnaryResponse
		afterFail *control.UnaryResponse
		expErr    error
		expFinal  bool
		expStaged bool
	}{
		"installed": {
			addCA:    control.MockMSResponse("h", nil, &mgmtpb.PoolAddCAResp{PoolUuid: poolUUID}),
			expFinal: true,
		},
		"install failed, CA absent": {
			addCA:     control.MockMSResponse("h", addErr, nil),
			afterFail: control.MockMSResponse("h", nil, caResp),
			expErr:    addErr,
		},
		"install failed, state unknown": {
			addCA:     control.MockMSResponse("h", addErr, nil),
			afterFail: control.MockMSResponse("h", errors.New("ms unreachable"), nil),
			expErr:    errors.New("install state unknown"),
			expStaged: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			dir := t.TempDir()
			caCertPath, caKeyPath := sectest.WriteCAFiles(t, dir)
			responses := []*control.UnaryResponse{
				control.MockMSResponse("h", nil, caResp), // dmg: pool UUID, already enabled?
				control.MockMSResponse("h", nil, caResp), // PoolAddCA: already enabled?
				tc.addCA,
			}
			if tc.afterFail != nil {
				responses = append(responses, tc.afterFail)
			}
			mi := control.NewMockInvoker(log, &control.MockInvokerConfig{UnaryResponseSet: responses})

			cmd := new(poolNodeAuthEnableCmd)
			cmd.setInvoker(mi)
			cmd.SetLog(log)
			cfg := control.DefaultConfig()
			cfg.TransportConfig.CARootPath = caCertPath
			cmd.setConfig(cfg)
			cmd.CAKey = caKeyPath
			cmd.Output = filepath.Join(dir, "pools")
			if err := cmd.Args.Pool.UnmarshalFlag(poolUUID); err != nil {
				t.Fatal(err)
			}

			test.CmpErr(t, tc.expErr, cmd.Execute(nil))

			certPath, keyPath := security.PoolCAPaths(cmd.Output, uuid.MustParse(poolUUID))
			for _, p := range []string{certPath, keyPath} {
				test.AssertEqual(t, tc.expFinal, fileExists(p), p)
				test.AssertEqual(t, tc.expStaged, fileExists(p+stagedSuffix), p+stagedSuffix)
			}
		})
	}
}
