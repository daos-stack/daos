//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"math/big"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
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
			AllowInsecure: false,
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

// testChain mints root CA -> pool CA -> leaf, writes the leaf files to
// dir as <uuid>.{crt,key} and the root to dir/daosCA.crt, and returns
// (rootPath, poolCAPEM) for wiring into the config and msInfo.
func testChain(t *testing.T, dir, cn string, notBefore time.Time) (string, []byte) {
	t.Helper()

	rootPEM, rootKey := sectest.NewCA(t, "Test DAOS CA", nil, nil)
	rootCert := sectest.ParseCert(t, rootPEM)
	poolCAPEM, poolCAKey := sectest.NewCA(t, "Test Pool CA", rootCert, rootKey)
	poolCACert := sectest.ParseCert(t, poolCAPEM)

	leafKey, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	serial, _ := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	tmpl := &x509.Certificate{
		SerialNumber: serial,
		Subject:      pkix.Name{CommonName: cn},
		NotBefore:    notBefore,
		NotAfter:     notBefore.Add(time.Hour),
		KeyUsage:     x509.KeyUsageDigitalSignature,
		ExtKeyUsage:  []x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth},
	}
	leafDER, err := x509.CreateCertificate(rand.Reader, tmpl, poolCACert, &leafKey.PublicKey, poolCAKey)
	if err != nil {
		t.Fatal(err)
	}
	leafPEM := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: leafDER})
	if err := os.WriteFile(filepath.Join(dir, checkTestPoolUUID.String()+".crt"), leafPEM, 0644); err != nil {
		t.Fatal(err)
	}
	keyDER, err := x509.MarshalPKCS8PrivateKey(leafKey)
	if err != nil {
		t.Fatal(err)
	}
	keyPEM := pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: keyDER})
	if err := os.WriteFile(filepath.Join(dir, checkTestPoolUUID.String()+".key"), keyPEM, 0400); err != nil {
		t.Fatal(err)
	}

	rootPath := filepath.Join(dir, "daosCA.crt")
	if err := os.WriteFile(rootPath, rootPEM, 0644); err != nil {
		t.Fatal(err)
	}
	return rootPath, poolCAPEM
}

func requiredMS(caBundle []byte) *msInfo {
	return &msInfo{
		poolUUID:   checkTestPoolUUID,
		requires:   true,
		caBundle:   caBundle,
		watermarks: security.CertWatermarks{},
	}
}

func notRequiredMS() *msInfo {
	return &msInfo{poolUUID: checkTestPoolUUID}
}

func checkByName(t *testing.T, c *nodeCertChecker, name string) nodeCertCheck {
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

	c := testCheckCmd(t, tmpDir).runChecks(&msInfo{err: errors.New("connection refused")})
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
	rootPath, poolCAPEM := testChain(t, tmpDir,
		security.CertCNPrefixNode+machine, time.Now().Add(-time.Minute))

	cmd := testCheckCmd(t, tmpDir)
	cmd.cfg.TransportConfig.CARootPath = rootPath

	c := cmd.runChecks(requiredMS(poolCAPEM))
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

	rootPath, poolCAPEM := testChain(t, tmpDir,
		security.CertCNPrefixTenant+"teamA", time.Now().Add(-time.Minute))

	cmd := testCheckCmd(t, tmpDir)
	cmd.cfg.TransportConfig.CARootPath = rootPath

	c := cmd.runChecks(requiredMS(poolCAPEM))
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

	rootPath, _ := testChain(t, tmpDir,
		security.CertCNPrefixTenant+"teamA", time.Now().Add(-time.Minute))
	// The pool's CURRENT CA is unrelated to the deployed cert's issuer.
	otherCAPEM, _ := sectest.NewCA(t, "Rotated-In CA", nil, nil)

	cmd := testCheckCmd(t, tmpDir)
	cmd.cfg.TransportConfig.CARootPath = rootPath

	c := cmd.runChecks(requiredMS(otherCAPEM))
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
	rootPath, poolCAPEM := testChain(t, tmpDir, security.CertCNPrefixTenant+"teamA", nb)

	cmd := testCheckCmd(t, tmpDir)
	cmd.cfg.TransportConfig.CARootPath = rootPath

	mi := requiredMS(poolCAPEM)
	mi.watermarks = security.CertWatermarks{
		security.CertCNPrefixTenant + "teamA": nb.Add(time.Minute),
	}
	c := cmd.runChecks(mi)
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

	sectest.WriteNodeCert(t, tmpDir, checkTestPoolUUID,
		security.CertCNPrefixTenant+"teamA",
		time.Now().Add(-time.Minute), time.Now().Add(time.Hour))

	c := testCheckCmd(t, tmpDir).runChecks(notRequiredMS())
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
		mi         *msInfo
		missingDir bool
		expFail    bool
	}{
		"not required, missing file": {mi: notRequiredMS()},
		"not required, missing dir":  {mi: notRequiredMS(), missingDir: true},
		"required, missing file": {
			mi:      requiredMS(nil),
			expFail: true,
		},
		"required, missing dir": {
			mi:         requiredMS(nil),
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

			c := testCheckCmd(t, dir).runChecks(tc.mi)
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
				sectest.WriteNodeCert(t, dir, checkTestPoolUUID,
					security.CertCNPrefixNode+"testhost",
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
				sectest.WriteNodeCert(t, dir, checkTestPoolUUID,
					security.CertCNPrefixNode+"otherhost",
					time.Now().Add(-time.Minute), time.Now().Add(time.Hour))
				return testCheckCmd(t, dir)
			},
			failCheck: "machine name",
		},
		"expired cert": {
			setup: func(t *testing.T, dir string) *checkNodeCertCmd {
				sectest.WriteNodeCert(t, dir, checkTestPoolUUID,
					security.CertCNPrefixNode+"testhost",
					time.Now().Add(-2*time.Hour), time.Now().Add(-time.Hour))
				return testCheckCmd(t, dir)
			},
			failCheck: "validity",
		},
	} {
		t.Run(name, func(t *testing.T) {
			tmpDir, cleanup := test.CreateTestDir(t)
			defer cleanup()

			c := tc.setup(t, tmpDir).runChecks(notRequiredMS())
			if !c.failed {
				t.Fatalf("expected failure, got %+v", c.checks)
			}
			if got := checkByName(t, c, tc.failCheck); got.OK {
				t.Errorf("expected %q to fail: %+v", tc.failCheck, got)
			}
		})
	}
}

func TestAgent_CheckNodeCert_NoDAOSCAConfiguredFails(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	_, poolCAPEM := testChain(t, tmpDir,
		security.CertCNPrefixTenant+"teamA", time.Now().Add(-time.Minute))

	cmd := testCheckCmd(t, tmpDir)
	cmd.cfg.TransportConfig.CARootPath = ""

	c := cmd.runChecks(requiredMS(poolCAPEM))
	if got := checkByName(t, c, "chain"); got.OK {
		t.Errorf("expected chain failure without a DAOS CA: %+v", got)
	}
	if !c.failed {
		t.Fatal("expected overall failure")
	}
}

func TestAgent_CheckNodeCert_WatermarkErrorFails(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	rootPath, poolCAPEM := testChain(t, tmpDir,
		security.CertCNPrefixTenant+"teamA", time.Now().Add(-time.Minute))

	cmd := testCheckCmd(t, tmpDir)
	cmd.cfg.TransportConfig.CARootPath = rootPath

	mi := requiredMS(poolCAPEM)
	mi.wmErr = errors.New("watermark rpc failed")
	c := cmd.runChecks(mi)
	if got := checkByName(t, c, "revocation"); got.OK {
		t.Errorf("expected revocation failure when watermarks are unavailable: %+v", got)
	}
	if !c.failed {
		t.Fatal("expected overall failure")
	}
}
