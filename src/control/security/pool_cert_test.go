//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"crypto/x509"
	"encoding/pem"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	sectest "github.com/daos-stack/daos/src/control/security/test"
)

func TestSecurity_CertPaths(t *testing.T) {
	poolUUID := uuid.MustParse("12345678-1234-1234-1234-123456789abc")

	certPath, keyPath := PoolCAPaths("/d", poolUUID)
	test.AssertEqual(t, "/d/12345678-1234-1234-1234-123456789abc_ca.crt", certPath, "pool CA cert")
	test.AssertEqual(t, "/d/12345678-1234-1234-1234-123456789abc_ca.key", keyPath, "pool CA key")
	certPath, keyPath = NodeCertPaths("/d", poolUUID)
	test.AssertEqual(t, "/d/12345678-1234-1234-1234-123456789abc.crt", certPath, "node cert")
	test.AssertEqual(t, "/d/12345678-1234-1234-1234-123456789abc.key", keyPath, "node key")
	test.AssertEqual(t, "/etc/daos/certs/pools", DefaultPoolCADir("/etc/daos/certs/admin.key"), "pool CA dir")
	test.AssertEqual(t, "/etc/daos/certs/daosCA.key", DefaultDAOSCAKeyPath("/etc/daos/certs/daosCA.crt"), "DAOS CA key")
}

func TestSecurity_DecodeCertWatermarks(t *testing.T) {
	for name, tc := range map[string]struct {
		input         string
		expWatermarks CertWatermarks
		expErr        error
	}{
		"empty object": {
			input:         `{}`,
			expWatermarks: CertWatermarks{},
		},
		"one entry": {
			input: `{"node:host1":"2026-01-02T03:04:05Z"}`,
			expWatermarks: CertWatermarks{
				"node:host1": time.Date(2026, 1, 2, 3, 4, 5, 0, time.UTC),
			},
		},
		"two entries": {
			input: `{"node:host1":"2026-01-02T03:04:05Z","tenant:t1":"2026-02-01T00:00:00Z"}`,
			expWatermarks: CertWatermarks{
				"node:host1": time.Date(2026, 1, 2, 3, 4, 5, 0, time.UTC),
				"tenant:t1":  time.Date(2026, 2, 1, 0, 0, 0, 0, time.UTC),
			},
		},
		"offset input": {
			input: `{"node:host1":"2026-01-02T04:04:05+01:00"}`,
			expWatermarks: CertWatermarks{
				"node:host1": time.Date(2026, 1, 2, 3, 4, 5, 0, time.UTC),
			},
		},
		"malformed JSON":   {input: `not json`, expErr: errors.New("parsing cert watermarks blob")},
		"bad timestamp":    {input: `{"node:host1":"not-a-time"}`, expErr: errors.New("parsing watermark for node:host1")},
		"missing timezone": {input: `{"node:host1":"2026-01-02T03:04:05"}`, expErr: errors.New("parsing watermark for node:host1")},
	} {
		t.Run(name, func(t *testing.T) {
			got, err := DecodeCertWatermarks([]byte(tc.input))
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expWatermarks, got); diff != "" {
				t.Fatalf("unexpected watermarks (-want, +got):\n%s\n", diff)
			}
			for cn, ts := range got {
				test.AssertEqual(t, time.UTC, ts.Location(), cn)
			}
		})
	}
}

func TestSecurity_CertWatermarks_RoundTrip(t *testing.T) {
	for name, tc := range map[string]struct {
		input   CertWatermarks
		expJSON string
	}{
		"single node": {
			input: CertWatermarks{
				"node:node1": time.Date(2026, 4, 15, 14, 0, 29, 0, time.UTC),
			},
			expJSON: `{"node:node1":"2026-04-15T14:00:29Z"}`,
		},
		"multiple": {
			input: CertWatermarks{
				"node:node1":   time.Date(2026, 4, 15, 14, 0, 29, 0, time.UTC),
				"node:node2":   time.Date(2026, 4, 15, 14, 0, 30, 0, time.UTC),
				"tenant:teamA": time.Date(2026, 4, 20, 9, 15, 0, 0, time.UTC),
			},
			expJSON: `{"node:node1":"2026-04-15T14:00:29Z","node:node2":"2026-04-15T14:00:30Z","tenant:teamA":"2026-04-20T09:15:00Z"}`,
		},
		"non-UTC input": {
			input: CertWatermarks{
				"node:node1": time.Date(2026, 4, 15, 15, 0, 29, 0, time.FixedZone("CET", 3600)),
			},
			expJSON: `{"node:node1":"2026-04-15T14:00:29Z"}`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			encoded, err := encodeCertWatermarks(tc.input)
			if err != nil {
				t.Fatal(err)
			}
			test.AssertEqual(t, tc.expJSON, string(encoded), "encoded")

			got, err := DecodeCertWatermarks(encoded)
			if err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(tc.input, got); diff != "" {
				t.Fatalf("unexpected watermarks (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestSecurity_advanceCertWatermark(t *testing.T) {
	t0 := time.Date(2026, 4, 15, 14, 0, 0, 0, time.UTC)
	cn := "node:n1"

	for name, tc := range map[string]struct {
		existing     CertWatermarks
		now          time.Time
		expWatermark time.Time
	}{
		"no prior":         {now: t0, expWatermark: t0},
		"prior for other":  {existing: CertWatermarks{"node:other": t0.Add(time.Hour)}, now: t0, expWatermark: t0},
		"now after prior":  {existing: CertWatermarks{cn: t0}, now: t0.Add(time.Minute), expWatermark: t0.Add(time.Minute)},
		"now equals prior": {existing: CertWatermarks{cn: t0}, now: t0, expWatermark: t0.Add(time.Second)},
		"now before prior": {existing: CertWatermarks{cn: t0.Add(time.Hour)}, now: t0, expWatermark: t0.Add(time.Hour + time.Second)},
		"sub-second now":   {now: t0.Add(999 * time.Millisecond), expWatermark: t0},
	} {
		t.Run(name, func(t *testing.T) {
			got := advanceCertWatermark(tc.existing, cn, tc.now)
			if diff := cmp.Diff(tc.expWatermark, got); diff != "" {
				t.Fatalf("unexpected watermark (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func bundleFingerprints(t *testing.T, bundle []byte) []string {
	t.Helper()

	entries, err := ParseCABundle(bundle)
	if err != nil {
		t.Fatal(err)
	}
	fps := make([]string, 0, len(entries))
	for _, e := range entries {
		fps = append(fps, e.Fingerprint)
	}
	return fps
}

func TestSecurity_RemoveCertByFingerprint(t *testing.T) {
	pemA, _ := sectest.NewCA(t, "CA-A", nil, nil)
	pemB, _ := sectest.NewCA(t, "CA-B", nil, nil)
	pemC, _ := sectest.NewCA(t, "CA-C", nil, nil)
	bundle := append(append(append([]byte{}, pemA...), pemB...), pemC...)
	fpA := CertFingerprint(sectest.ParseCert(t, pemA))
	fpB := CertFingerprint(sectest.ParseCert(t, pemB))
	fpC := CertFingerprint(sectest.ParseCert(t, pemC))

	for name, tc := range map[string]struct {
		fingerprint  string
		expRemaining []string
		expErr       error
	}{
		"first":     {fingerprint: fpA, expRemaining: []string{fpB, fpC}},
		"middle":    {fingerprint: fpB, expRemaining: []string{fpA, fpC}},
		"last":      {fingerprint: fpC, expRemaining: []string{fpA, fpB}},
		"not found": {fingerprint: "deadbeef", expErr: errors.New("no CA with fingerprint")},
	} {
		t.Run(name, func(t *testing.T) {
			remaining, removed, err := RemoveCertByFingerprint(bundle, tc.fingerprint)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, 1, removed, "removed")
			if diff := cmp.Diff(tc.expRemaining, bundleFingerprints(t, remaining)); diff != "" {
				t.Fatalf("unexpected remaining bundle (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestSecurity_ParsePoolCACert(t *testing.T) {
	caPEM, _ := sectest.NewCA(t, "valid CA", nil, nil)
	f := newCertFixture(t)
	leaf := f.leaf(t, PoolCertCNPrefixNode+"m")

	for name, tc := range map[string]struct {
		input  []byte
		expErr error
	}{
		"valid CA":   {input: caPEM},
		"not a CA":   {input: leaf.CertPEM, expErr: errors.New("IsCA")},
		"empty":      {expErr: errors.New("exactly one CERTIFICATE")},
		"two blocks": {input: append(append([]byte{}, caPEM...), caPEM...), expErr: errors.New("exactly one CERTIFICATE")},
		"truncated":  {input: caPEM[:len(caPEM)-20], expErr: errors.New("trailing non-PEM")},
	} {
		t.Run(name, func(t *testing.T) {
			_, err := ParsePoolCACert(tc.input)
			test.CmpErr(t, tc.expErr, err)
		})
	}
}

func TestSecurity_CheckPoolCAIdentity(t *testing.T) {
	poolUUID := uuid.MustParse("9bd63a8b-1c5f-4c3a-9a63-7f3b1d0e2a11")

	for name, tc := range map[string]struct {
		cn     string
		expErr error
	}{
		"matches":     {cn: poolCACommonName(poolUUID)},
		"literal CN":  {cn: "DAOS Pool CA 9bd63a8b-1c5f-4c3a-9a63-7f3b1d0e2a11"},
		"other pool":  {cn: poolCACommonName(uuid.Nil), expErr: errors.New("is not for pool")},
		"not pool CA": {cn: "Admin CA", expErr: errors.New("is not for pool")},
	} {
		t.Run(name, func(t *testing.T) {
			certPEM, _ := sectest.NewCA(t, tc.cn, nil, nil)
			test.CmpErr(t, tc.expErr, CheckPoolCAIdentity(sectest.ParseCert(t, certPEM), poolUUID))
		})
	}
}

func TestSecurity_VerifyPoolCAChain(t *testing.T) {
	f := newCertFixture(t)
	other := newCertFixture(t)

	for name, tc := range map[string]struct {
		cert   *x509.Certificate
		root   *x509.Certificate
		expErr error
	}{
		"chained":    {cert: f.poolCA.Cert, root: f.root},
		"no root":    {cert: f.poolCA.Cert, expErr: ErrInvalidInput},
		"other root": {cert: other.poolCA.Cert, root: f.root, expErr: errors.New("does not chain")},
	} {
		t.Run(name, func(t *testing.T) {
			test.CmpErr(t, tc.expErr, VerifyPoolCAChain(tc.cert, tc.root))
		})
	}
}

func TestSecurity_ParseCABundle(t *testing.T) {
	pemA, _ := sectest.NewCA(t, "A", nil, nil)
	pemB, _ := sectest.NewCA(t, "B", nil, nil)
	fpA := CertFingerprint(sectest.ParseCert(t, pemA))
	fpB := CertFingerprint(sectest.ParseCert(t, pemB))
	notACert := pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: []byte("x")})
	garbage := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: []byte("not der")})

	for name, tc := range map[string]struct {
		bundle          []byte
		expFingerprints []string
		expErr          error
	}{
		"empty":          {expFingerprints: []string{}},
		"one":            {bundle: pemA, expFingerprints: []string{fpA}},
		"two":            {bundle: append(append([]byte{}, pemA...), pemB...), expFingerprints: []string{fpA, fpB}},
		"text before":    {bundle: append([]byte("subject=CN=A\n"), pemA...), expFingerprints: []string{fpA}},
		"non-cert block": {bundle: append(append([]byte{}, pemA...), notACert...), expErr: errors.New("unexpected PEM block type")},
		"malformed cert": {bundle: append(append([]byte{}, pemA...), garbage...), expErr: errors.New("malformed cert")},
		"truncated":      {bundle: append(append([]byte{}, pemA...), pemB[:len(pemB)-20]...), expErr: errors.New("trailing non-PEM")},
	} {
		t.Run(name, func(t *testing.T) {
			entries, err := ParseCABundle(tc.bundle)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			fps := make([]string, 0, len(entries))
			for _, e := range entries {
				test.AssertEqual(t, CertFingerprint(e.Cert), e.Fingerprint, "fingerprint")
				fps = append(fps, e.Fingerprint)
			}
			if diff := cmp.Diff(tc.expFingerprints, fps); diff != "" {
				t.Fatalf("unexpected bundle (-want, +got):\n%s\n", diff)
			}
		})
	}
}
