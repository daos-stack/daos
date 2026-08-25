//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"crypto/x509"
	"encoding/pem"
	"testing"
	"time"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	sectest "github.com/daos-stack/daos/src/control/security/test"
)

func parseTestCert(t *testing.T, certPEM []byte) *x509.Certificate {
	t.Helper()
	block, _ := pem.Decode(certPEM)
	if block == nil {
		t.Fatal("no PEM data in generated cert")
	}
	cert, err := x509.ParseCertificate(block.Bytes)
	if err != nil {
		t.Fatal(err)
	}
	return cert
}

// A revoked CN's replacement cert must be postdated past the watermark,
// or it is revoked at birth.
func TestControl_PoolGenerateClientCerts_WatermarkPostdating(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()
	caKeyPath, _ := sectest.WriteCAFiles(t, tmpDir)

	futureWM := time.Now().UTC().Add(time.Hour).Truncate(time.Second)
	pastWM := time.Now().UTC().Add(-time.Hour).Truncate(time.Second)

	certs, err := PoolGenerateClientCerts(&PoolGenerateClientCertsReq{
		CAKeyPath: caKeyPath,
		Nodes:     []string{"revoked", "clean"},
		Watermarks: security.CertWatermarks{
			security.CertCNPrefixNode + "revoked": futureWM,
			security.CertCNPrefixNode + "old":     pastWM,
		},
	})
	if err != nil {
		t.Fatal(err)
	}

	for _, c := range certs {
		nb := parseTestCert(t, c.CertPEM).NotBefore
		switch c.Name {
		case "revoked":
			if !nb.After(futureWM) {
				t.Errorf("cert for %s not postdated past watermark: NotBefore=%s, watermark=%s",
					c.CN, nb, futureWM)
			}
		case "clean":
			if nb.After(time.Now().UTC().Add(time.Minute)) {
				t.Errorf("cert for %s unexpectedly postdated: NotBefore=%s", c.CN, nb)
			}
		}
	}
}

// A watermark in the current second must still be bumped past: the
// RFC3339 watermark and the cert's ASN.1 NotBefore both truncate to
// whole seconds, so a full-precision comparison ties and mints a
// revoked-at-birth cert (caught live by test_revoke_reissue_works).
func TestControl_PoolGenerateClientCerts_SameSecondWatermark(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()
	caKeyPath, _ := sectest.WriteCAFiles(t, tmpDir)

	wm := time.Now().UTC().Truncate(time.Second)
	certs, err := PoolGenerateClientCerts(&PoolGenerateClientCertsReq{
		CAKeyPath: caKeyPath,
		Nodes:     []string{"host1"},
		Watermarks: security.CertWatermarks{
			security.CertCNPrefixNode + "host1": wm,
		},
	})
	if err != nil {
		t.Fatal(err)
	}

	nb := parseTestCert(t, certs[0].CertPEM).NotBefore
	if !nb.After(wm) {
		t.Errorf("NotBefore %s not strictly after same-second watermark %s", nb, wm)
	}
}

// A CN whose watermark is in the past mints normally from now.
func TestControl_PoolGenerateClientCerts_PastWatermark(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()
	caKeyPath, _ := sectest.WriteCAFiles(t, tmpDir)

	pastWM := time.Now().UTC().Add(-time.Hour).Truncate(time.Second)
	certs, err := PoolGenerateClientCerts(&PoolGenerateClientCertsReq{
		CAKeyPath: caKeyPath,
		Nodes:     []string{"host1"},
		Watermarks: security.CertWatermarks{
			security.CertCNPrefixNode + "host1": pastWM,
		},
	})
	if err != nil {
		t.Fatal(err)
	}

	nb := parseTestCert(t, certs[0].CertPEM).NotBefore
	if nb.Before(pastWM) || nb.After(time.Now().UTC().Add(time.Minute)) {
		t.Errorf("expected NotBefore near now, got %s", nb)
	}
}

func TestControl_PoolSetupCertAuth_ExistingCA(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	rootPEM, rootKey := sectest.NewCA(t, "DAOS Test Root", nil, nil)
	root := sectest.ParseCert(t, rootPEM)
	poolCAPEM, _ := sectest.NewCA(t, "Pool CA", root, rootKey)
	installed := &mgmt.PoolGetCAResp{PoolUuid: test.MockUUID(), CaBundle: poolCAPEM}
	added := &mgmt.PoolAddCAResp{PoolUuid: test.MockUUID()}

	for name, tc := range map[string]struct {
		append    bool
		responses []*UnaryResponse
		expErr    error
	}{
		"enable on an enabled pool is refused": {
			responses: []*UnaryResponse{MockMSResponse("host1", nil, installed)},
			expErr:    ErrNodeAuthEnabled,
		},
		"append on an enabled pool proceeds": {
			append:    true,
			responses: []*UnaryResponse{MockMSResponse("host1", nil, added)},
		},
		"enable fails when the CA query fails": {
			responses: []*UnaryResponse{MockMSResponse("host1", errors.New("ms unreachable"), nil)},
			expErr:    errors.New("ms unreachable"),
		},
		"enable on a pool with no CA proceeds": {
			responses: []*UnaryResponse{
				MockMSResponse("host1", nil, &mgmt.PoolGetCAResp{PoolUuid: test.MockUUID()}),
				MockMSResponse("host1", nil, added),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			mi := NewMockInvoker(log, &MockInvokerConfig{UnaryResponseSet: tc.responses})
			_, err := PoolSetupCertAuth(context.Background(), mi, &PoolSetupCertAuthReq{
				ID:      test.MockUUID(),
				CertPEM: poolCAPEM,
				Append:  tc.append,
			})
			if tc.expErr != nil {
				test.CmpErr(t, tc.expErr, err)
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
		})
	}
}

func TestControl_PoolIssueClientCerts(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	dir := t.TempDir()
	caKeyPath, _ := sectest.WriteCAFiles(t, dir)
	watermark := time.Now().Add(-time.Hour).Truncate(time.Second)
	wmBytes, err := security.EncodeCertWatermarks(security.CertWatermarks{
		security.CertCNPrefixNode + "client01": watermark,
	})
	if err != nil {
		t.Fatal(err)
	}
	wmResp := &mgmt.PoolGetCertWatermarksResp{Watermarks: wmBytes}
	revoked := &mgmt.PoolRevokeClientResp{PoolUuid: test.MockUUID(), WatermarkRfc3339: watermark.Format(time.RFC3339)}

	for name, tc := range map[string]struct {
		req       *PoolIssueClientCertsReq
		responses []*UnaryResponse
		expErr    string
		expRPCs   int
	}{
		"nodes and tenants together": {
			req:    &PoolIssueClientCertsReq{ID: "tank", CAKeyPath: caKeyPath, Nodes: []string{"a"}, Tenants: []string{"t"}},
			expErr: "not both",
		},
		"replace with tenants": {
			req:    &PoolIssueClientCertsReq{ID: "tank", CAKeyPath: caKeyPath, Tenants: []string{"t"}, Replace: true},
			expErr: "node certificates only",
		},
		"issue postdates past the watermark": {
			req:       &PoolIssueClientCertsReq{ID: "tank", CAKeyPath: caKeyPath, Nodes: []string{"client01"}},
			responses: []*UnaryResponse{MockMSResponse("host1", nil, wmResp)},
			expRPCs:   1,
		},
		"replace revokes before reading watermarks": {
			req: &PoolIssueClientCertsReq{ID: "tank", CAKeyPath: caKeyPath, Nodes: []string{"client01"}, Replace: true},
			responses: []*UnaryResponse{
				MockMSResponse("host1", nil, revoked),
				MockMSResponse("host1", nil, wmResp),
			},
			expRPCs: 2,
		},
	} {
		t.Run(name, func(t *testing.T) {
			mi := NewMockInvoker(log, &MockInvokerConfig{UnaryResponseSet: tc.responses})
			certs, err := PoolIssueClientCerts(context.Background(), mi, tc.req)
			if tc.expErr != "" {
				test.CmpErr(t, errors.New(tc.expErr), err)
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			test.AssertEqual(t, tc.expRPCs, mi.invokeCount, "unexpected RPC count")
			cert := parseTestCert(t, certs[0].CertPEM)
			if !cert.NotBefore.After(watermark) {
				t.Fatalf("NotBefore %s not past watermark %s", cert.NotBefore, watermark)
			}
		})
	}
}

func TestControl_clipNotAfter(t *testing.T) {
	notBefore := time.Date(2026, 8, 26, 0, 0, 0, 0, time.UTC)
	signer := &x509.Certificate{NotAfter: notBefore.Add(400 * 24 * time.Hour)}
	for name, tc := range map[string]struct {
		validity time.Duration
		exp      time.Time
	}{
		"zero means the signer's expiry": {validity: 0, exp: signer.NotAfter},
		"shorter than the signer":        {validity: 30 * 24 * time.Hour, exp: notBefore.Add(30 * 24 * time.Hour)},
		"longer is clipped":              {validity: 1000 * 24 * time.Hour, exp: signer.NotAfter},
	} {
		t.Run(name, func(t *testing.T) {
			if got := clipNotAfter(notBefore, tc.validity, signer); !got.Equal(tc.exp) {
				t.Fatalf("got %s, want %s", got, tc.exp)
			}
		})
	}
}
