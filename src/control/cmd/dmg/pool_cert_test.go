//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"crypto/x509"
	"encoding/json"
	"encoding/pem"
	"io"
	"os"
	"path/filepath"
	"strings"
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

func TestDmg_PoolNodeAuthGenerateCert_NodeHostlist(t *testing.T) {
	poolUUID := uuid.MustParse(test.MockUUID())
	dir := t.TempDir()
	caPEM, caKey := sectest.NewCA(t, "Pool CA", nil, nil)
	caKeyDER, err := x509.MarshalPKCS8PrivateKey(caKey)
	if err != nil {
		t.Fatal(err)
	}
	caCertPath, caKeyPath := security.PoolCAPaths(dir, poolUUID)
	sectest.WriteCertFiles(t, caCertPath, caKeyPath, caPEM,
		pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: caKeyDER}))

	for name, tc := range map[string]struct {
		args   string
		expDir []string
		expErr error
	}{
		"single": {
			args:   "--node client01",
			expDir: []string{"client01"},
		},
		"range": {
			args:   "--node client[01-03]",
			expDir: []string{"client01", "client02", "client03"},
		},
		"repeated flags mixed with ranges": {
			args:   "--node client[01-02] --node login --node gpu[7,9]",
			expDir: []string{"client01", "client02", "gpu7", "gpu9", "login"},
		},
		"overlapping flags": {
			args:   "--node client[01-02] --node client01",
			expDir: []string{"client01", "client02"},
		},
		"invalid name after expansion": {
			args:   "--node client[01-02]/x",
			expErr: errors.New("disallowed character"),
		},
		"dotted name after expansion": {
			args:   "--node client[1-2].example.com",
			expErr: errors.New("short host name"),
		},
		"unterminated range": {
			args:   "--node client[01-03",
			expErr: errors.New("client[01-03"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			out := filepath.Join(t.TempDir(), "certs")
			cmd := strings.Join([]string{"pool", "node-auth", "generate-cert", poolUUID.String(),
				"--pool-ca-key", caKeyPath, "--output", out, tc.args}, " ")
			test.CmpErr(t, tc.expErr, runCmd(t, cmd, log, control.NewMockInvoker(log, nil)))

			var got []string
			entries, err := os.ReadDir(out)
			if err != nil && tc.expErr == nil {
				t.Fatal(err)
			}
			for _, e := range entries {
				got = append(got, e.Name())
			}
			test.AssertEqual(t, strings.Join(tc.expDir, ","), strings.Join(got, ","), "staged names")
		})
	}
}

func TestDmg_PoolNodeAuthStatus_Issued(t *testing.T) {
	poolUUID := uuid.MustParse(test.MockUUID())
	caPEM, caKey := sectest.NewCA(t, "Pool CA", nil, nil)
	ca := sectest.ParseCert(t, caPEM)
	now := time.Now()

	stage := func(t *testing.T, dir, name string, notAfter time.Time) {
		t.Helper()
		certPEM, keyPEM := sectest.NewLeaf(t, "node:"+name, ca, caKey, now.Add(-time.Hour), notAfter)
		certPath, keyPath := security.NodeCertPaths(filepath.Join(dir, name), poolUUID)
		if err := os.MkdirAll(filepath.Dir(certPath), 0700); err != nil {
			t.Fatal(err)
		}
		sectest.WriteCertFiles(t, certPath, keyPath, certPEM, keyPEM)
	}

	staged := t.TempDir()
	stage(t, staged, "client01", now.Add(300*24*time.Hour))
	stage(t, staged, "client02", now.Add(10*24*time.Hour))
	notAfterSoon := now.Add(10 * 24 * time.Hour).UTC().Truncate(time.Second).Format(time.RFC3339)

	missing := filepath.Join(t.TempDir(), "nope")

	run := func(t *testing.T, caBundle []byte, args string) (string, error) {
		t.Helper()
		log, buf := logging.NewTestLogger(t.Name())
		defer test.ShowBufferOnFailure(t, buf)
		mi := &bridgeConnInvoker{MockInvoker: *control.DefaultMockInvoker(log), t: t, conn: newTestConn(t), caBundle: caBundle}
		err := runCmd(t, "pool node-auth status "+poolUUID.String()+" "+args, log, mi)
		return buf.String(), err
	}

	t.Run("text", func(t *testing.T) {
		out, err := run(t, nil, "--issued --output "+staged)
		if err != nil {
			t.Fatal(err)
		}
		for _, exp := range []string{
			"Issued certificates: 2",
			"node:client01  ",
			"node:client02  " + notAfterSoon,
			"WARNING: node:client02 expires in 9 day(s)",
		} {
			if !strings.Contains(out, exp) {
				t.Fatalf("missing %q in:\n%s", exp, out)
			}
		}
		if strings.Contains(out, "node:client01 expire") {
			t.Fatalf("unexpected warning for client01 in:\n%s", out)
		}
	})

	t.Run("json", func(t *testing.T) {
		r, w, err := os.Pipe()
		if err != nil {
			t.Fatal(err)
		}
		stdout := os.Stdout
		os.Stdout = w
		defer func() { os.Stdout = stdout }()
		_, err = run(t, nil, "--json --issued --output "+staged)
		w.Close()
		if err != nil {
			t.Fatal(err)
		}
		outBytes, err := io.ReadAll(r)
		if err != nil {
			t.Fatal(err)
		}
		out := string(outBytes)
		var resp struct {
			Response struct {
				Issued []issuedCertInfo `json:"issued"`
			} `json:"response"`
		}
		if err := json.Unmarshal([]byte(out), &resp); err != nil {
			t.Fatalf("%s: %s", err, out)
		}
		test.AssertEqual(t, 2, len(resp.Response.Issued), "issued certs")
		test.AssertEqual(t, "node:client02", resp.Response.Issued[1].CN, "second CN")
		test.AssertEqual(t, notAfterSoon, resp.Response.Issued[1].NotAfter, "second NotAfter")
	})

	t.Run("enabled, empty directory", func(t *testing.T) {
		out, err := run(t, caPEM, "--issued --output "+t.TempDir())
		if err != nil {
			t.Fatal(err)
		}
		for _, exp := range []string{"Node authentication: enabled", "Issued certificates: 0"} {
			if !strings.Contains(out, exp) {
				t.Fatalf("missing %q in:\n%s", exp, out)
			}
		}
	})

	t.Run("without --issued", func(t *testing.T) {
		out, err := run(t, nil, "--output "+staged)
		if err != nil {
			t.Fatal(err)
		}
		if strings.Contains(out, "Issued certificates") {
			t.Fatalf("unexpected issued listing in:\n%s", out)
		}
	})

	t.Run("missing directory", func(t *testing.T) {
		_, err := run(t, nil, "--issued --output "+missing)
		test.CmpErr(t, errors.New(missing), err)
	})

	t.Run("subdirectory without a certificate", func(t *testing.T) {
		bad := t.TempDir()
		if err := os.Mkdir(filepath.Join(bad, "client09"), 0700); err != nil {
			t.Fatal(err)
		}
		_, err := run(t, nil, "--issued --output "+bad)
		test.CmpErr(t, errors.New("client09"), err)
	})
}
