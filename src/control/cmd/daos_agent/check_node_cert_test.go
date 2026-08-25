//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/security/auth"
	sectest "github.com/daos-stack/daos/src/control/security/test"
)

var checkTestPoolUUID = uuid.MustParse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")

func testCheckCmd(t *testing.T, certDir string) *checkNodeCertCmd {
	t.Helper()
	cmd := &checkNodeCertCmd{}
	cmd.cfg = &Config{
		SystemName: "daos_server",
		TransportConfig: &security.TransportConfig{
			AllowInsecure:     false,
			CertificateConfig: security.CertificateConfig{CertMaxClockSkew: security.DefaultCertMaxClockSkew},
		},
		CredentialConfig: &security.CredentialConfig{
			NodeCertDir: certDir,
		},
	}
	log, _ := logging.NewTestLogger(t.Name())
	cmd.SetLog(log)
	cmd.Args.Pool = checkTestPoolUUID.String()
	return cmd
}

// testChain mints root CA -> pool CA -> leaf, deploys the leaf as the pool's
// node cert and the root to dir/daosCA.crt, and returns (rootPath, poolCAPEM).
func testChain(t *testing.T, dir string, poolUUID uuid.UUID, cn string, notBefore, notAfter time.Time) (string, []byte) {
	t.Helper()

	rootPEM, rootKey := sectest.NewCA(t, "Test DAOS CA", nil, nil)
	poolCA, err := security.GeneratePoolCA(poolUUID, sectest.ParseCert(t, rootPEM), rootKey, 0)
	if err != nil {
		t.Fatal(err)
	}
	leafPEM, leafKeyPEM := sectest.NewLeaf(t, cn, poolCA.Cert, poolCA.Key, notBefore, notAfter)
	certPath, keyPath := security.NodeCertPaths(dir, poolUUID)
	sectest.WriteCertFiles(t, certPath, keyPath, leafPEM, leafKeyPEM)

	rootPath := filepath.Join(dir, "daosCA.crt")
	if err := os.WriteFile(rootPath, rootPEM, 0644); err != nil {
		t.Fatal(err)
	}
	return rootPath, poolCA.CertPEM
}

// requiredMS is a pool with node auth enabled; a nil bundle gets a CA of its own.
func requiredMS(t *testing.T, caBundle []byte) *control.PoolNodeAuthState {
	t.Helper()
	if caBundle == nil {
		caBundle, _ = sectest.NewCA(t, "Some Pool CA", nil, nil)
	}
	cas, err := security.ParseCABundle(caBundle)
	if err != nil {
		t.Fatal(err)
	}
	return &control.PoolNodeAuthState{
		PoolUUID:   checkTestPoolUUID,
		CABundle:   caBundle,
		CAs:        cas,
		Watermarks: security.CertWatermarks{},
	}
}

func notRequiredMS() *control.PoolNodeAuthState {
	return &control.PoolNodeAuthState{PoolUUID: checkTestPoolUUID}
}

func checkByName(t *testing.T, c *nodeCertReport, name string) nodeCertCheck {
	t.Helper()
	for _, chk := range c.checks {
		if chk.Name == name {
			return chk
		}
	}
	t.Fatalf("no check named %q in %+v", name, c.checks)
	return nodeCertCheck{}
}

// MS reachability is the first checked link: without it the agent
// cannot serve the pool at all, so nothing cert-related is evaluated.
func TestAgent_CheckNodeCert_MSUnreachable(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	c := testCheckCmd(t, tmpDir).runChecks(nil, errors.New("connection refused"))
	if !c.failed {
		t.Fatalf("expected failure, got %+v", c.checks)
	}
	if got := checkByName(t, c, "management service"); got.OK {
		t.Errorf("expected management service check to fail: %+v", got)
	}
	if len(c.checks) != 1 {
		t.Fatalf("expected no further checks, got %+v", c.checks)
	}
}

// A node-scoped cert for this machine passes the machine-name check.
func TestAgent_CheckNodeCert_NodeCertMatch(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	machine, err := auth.GetMachineName()
	if err != nil {
		t.Fatal(err)
	}
	rootPath, poolCAPEM := testChain(t, tmpDir, checkTestPoolUUID,
		security.PoolCertCNPrefixNode+machine, time.Now().Add(-time.Minute), time.Now().Add(time.Hour))

	cmd := testCheckCmd(t, tmpDir)
	cmd.cfg.TransportConfig.CARootPath = rootPath

	c := cmd.runChecks(requiredMS(t, poolCAPEM), nil)
	if c.failed {
		t.Fatalf("expected all checks to pass, got %+v", c.checks)
	}
	if got := checkByName(t, c, "machine name"); got.Value != machine+"  (match)" {
		t.Errorf("unexpected machine name result: %+v", got)
	}
}

// The definitive server-backed pass: chain verifies against the pool's
// current CA and the CN is not revoked.
func TestAgent_CheckNodeCert_ServerBacked_AllPass(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	rootPath, poolCAPEM := testChain(t, tmpDir, checkTestPoolUUID,
		security.PoolCertCNPrefixTenant+"teamA", time.Now().Add(-time.Minute), time.Now().Add(time.Hour))

	cmd := testCheckCmd(t, tmpDir)
	cmd.cfg.TransportConfig.CARootPath = rootPath

	c := cmd.runChecks(requiredMS(t, poolCAPEM), nil)
	if c.failed {
		t.Fatalf("expected all checks to pass, got %+v", c.checks)
	}
	if got := checkByName(t, c, "chain"); got.Value != "verifies against the pool's current CA" {
		t.Errorf("unexpected chain result: %+v", got)
	}
	if got := checkByName(t, c, "revocation"); got.Value != "not revoked" {
		t.Errorf("unexpected revocation result: %+v", got)
	}
}

// A cert from a rotated-away CA must fail the chain check.
func TestAgent_CheckNodeCert_StaleCAFails(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	rootPath, _ := testChain(t, tmpDir, checkTestPoolUUID,
		security.PoolCertCNPrefixTenant+"teamA", time.Now().Add(-time.Minute), time.Now().Add(time.Hour))
	// The pool's CURRENT CA is unrelated to the deployed cert's issuer.
	otherCAPEM, _ := sectest.NewCA(t, "Rotated-In CA", nil, nil)

	cmd := testCheckCmd(t, tmpDir)
	cmd.cfg.TransportConfig.CARootPath = rootPath

	c := cmd.runChecks(requiredMS(t, otherCAPEM), nil)
	if got := checkByName(t, c, "chain"); got.OK {
		t.Errorf("expected chain failure against rotated CA: %+v", got)
	}
	if !c.failed {
		t.Fatal("expected overall failure")
	}
}

// A cert at or below its CN's watermark is revoked.
func TestAgent_CheckNodeCert_RevokedFails(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	nb := time.Now().Add(-time.Minute).Truncate(time.Second)
	rootPath, poolCAPEM := testChain(t, tmpDir, checkTestPoolUUID,
		security.PoolCertCNPrefixTenant+"teamA", nb, nb.Add(time.Hour))

	cmd := testCheckCmd(t, tmpDir)
	cmd.cfg.TransportConfig.CARootPath = rootPath

	mi := requiredMS(t, poolCAPEM)
	mi.Watermarks = security.CertWatermarks{
		security.PoolCertCNPrefixTenant + "teamA": nb.Add(time.Minute),
	}
	c := cmd.runChecks(mi, nil)
	if got := checkByName(t, c, "revocation"); got.OK {
		t.Errorf("expected revocation failure: %+v", got)
	}
	if !c.failed {
		t.Fatal("expected overall failure")
	}
}

func TestAgent_CheckNodeCert_TenantCert(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	testChain(t, tmpDir, checkTestPoolUUID, security.PoolCertCNPrefixTenant+"teamA",
		time.Now().Add(-time.Minute), time.Now().Add(time.Hour))

	c := testCheckCmd(t, tmpDir).runChecks(notRequiredMS(), nil)
	if c.failed {
		t.Fatalf("expected all checks to pass, got %+v", c.checks)
	}
	if got := checkByName(t, c, "machine name"); !got.OK {
		t.Errorf("tenant cert should not require machine match: %+v", got)
	}
}

// Absence outcome depends on what the pool requires: passthrough when
// not required or unknowable, hard failure when the MS says certs are
// required.
func TestAgent_CheckNodeCert_Absence(t *testing.T) {
	for name, tc := range map[string]struct {
		mi         *control.PoolNodeAuthState
		missingDir bool
		expFail    bool
	}{
		"not required, missing file": {mi: notRequiredMS()},
		"not required, missing dir":  {mi: notRequiredMS(), missingDir: true},
		"required, missing file": {
			mi:      requiredMS(t, nil),
			expFail: true,
		},
		"required, missing dir": {
			mi:         requiredMS(t, nil),
			missingDir: true,
			expFail:    true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			tmpDir, cleanup := test.CreateTestDir(t)
			defer cleanup()
			dir := tmpDir
			if tc.missingDir {
				dir = tmpDir + "/nonexistent"
			}

			c := testCheckCmd(t, dir).runChecks(tc.mi, nil)
			if tc.expFail && !c.failed {
				t.Fatalf("expected failure when pool requires certs: %+v", c.checks)
			}
			if !tc.expFail {
				if c.failed {
					t.Fatalf("absence must not fail here: %+v", c.checks)
				}
				if !c.notDeployed {
					t.Fatalf("expected notDeployed state: %+v", c.checks)
				}
			}
		})
	}
}

func TestAgent_CheckNodeCert_Failures(t *testing.T) {
	for name, tc := range map[string]struct {
		setup     func(t *testing.T, dir string) *checkNodeCertCmd
		failCheck string
	}{
		"half-deployed: cert without key": {
			setup: func(t *testing.T, dir string) *checkNodeCertCmd {
				testChain(t, dir, checkTestPoolUUID, security.PoolCertCNPrefixNode+"testhost",
					time.Now().Add(-time.Minute), time.Now().Add(time.Hour))
				if err := os.Remove(
					filepath.Join(dir, checkTestPoolUUID.String()+".key")); err != nil {
					t.Fatal(err)
				}
				return testCheckCmd(t, dir)
			},
			failCheck: "key file",
		},
		"wrong machine": {
			setup: func(t *testing.T, dir string) *checkNodeCertCmd {
				testChain(t, dir, checkTestPoolUUID, security.PoolCertCNPrefixNode+"otherhost",
					time.Now().Add(-time.Minute), time.Now().Add(time.Hour))
				return testCheckCmd(t, dir)
			},
			failCheck: "machine name",
		},
		"expired cert": {
			setup: func(t *testing.T, dir string) *checkNodeCertCmd {
				testChain(t, dir, checkTestPoolUUID, security.PoolCertCNPrefixNode+"testhost",
					time.Now().Add(-2*time.Hour), time.Now().Add(-time.Hour))
				return testCheckCmd(t, dir)
			},
			failCheck: "validity",
		},
		"key readable by others": {
			setup: func(t *testing.T, dir string) *checkNodeCertCmd {
				testChain(t, dir, checkTestPoolUUID, security.PoolCertCNPrefixNode+"testhost",
					time.Now().Add(-time.Minute), time.Now().Add(time.Hour))
				_, keyPath := security.NodeCertPaths(dir, checkTestPoolUUID)
				if err := os.Chmod(keyPath, 0644); err != nil {
					t.Fatal(err)
				}
				return testCheckCmd(t, dir)
			},
			failCheck: "key file",
		},
	} {
		t.Run(name, func(t *testing.T) {
			tmpDir, cleanup := test.CreateTestDir(t)
			defer cleanup()

			c := tc.setup(t, tmpDir).runChecks(notRequiredMS(), nil)
			if !c.failed {
				t.Fatalf("expected failure, got %+v", c.checks)
			}
			if got := checkByName(t, c, tc.failCheck); got.OK {
				t.Errorf("expected %q to fail: %+v", tc.failCheck, got)
			}
		})
	}
}

func TestAgent_CheckNodeCert_NotYetValidWarns(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	// A tenant cert keeps the machine-name row out of the picture.
	testChain(t, tmpDir, checkTestPoolUUID, security.PoolCertCNPrefixTenant+"team",
		time.Now().Add(time.Hour), time.Now().Add(2*time.Hour))
	c := testCheckCmd(t, tmpDir).runChecks(notRequiredMS(), nil)
	if c.failed {
		t.Fatalf("expected an early cert to pass with a warning, got %+v", c.checks)
	}
	if got := checkByName(t, c, "validity"); !got.OK || !got.Warn {
		t.Errorf("expected validity to warn: %+v", got)
	}
}

func TestAgent_CheckNodeCert_NoDAOSCAConfiguredFails(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	_, poolCAPEM := testChain(t, tmpDir, checkTestPoolUUID,
		security.PoolCertCNPrefixTenant+"teamA", time.Now().Add(-time.Minute), time.Now().Add(time.Hour))

	cmd := testCheckCmd(t, tmpDir)
	cmd.cfg.TransportConfig.CARootPath = ""

	c := cmd.runChecks(requiredMS(t, poolCAPEM), nil)
	if got := checkByName(t, c, "chain"); got.OK {
		t.Errorf("expected chain failure without a DAOS CA: %+v", got)
	}
	if !c.failed {
		t.Fatal("expected overall failure")
	}
}
