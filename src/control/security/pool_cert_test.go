//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"encoding/pem"
	"strings"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/uuid"
	"github.com/pkg/errors"

	sectest "github.com/daos-stack/daos/src/control/security/test"
)

func TestSecurity_DecodeCertWatermarks(t *testing.T) {
	for name, tc := range map[string]struct {
		input   string
		wantLen int
		wantErr bool
	}{
		"empty object":     {`{}`, 0, false},
		"one entry":        {`{"node:host1":"2026-01-02T03:04:05Z"}`, 1, false},
		"two entries":      {`{"node:host1":"2026-01-02T03:04:05Z","tenant:t1":"2026-02-01T00:00:00Z"}`, 2, false},
		"malformed JSON":   {`not json`, 0, true},
		"bad timestamp":    {`{"node:host1":"not-a-time"}`, 0, true},
		"missing timezone": {`{"node:host1":"2026-01-02T03:04:05"}`, 0, true},
	} {
		t.Run(name, func(t *testing.T) {
			got, err := DecodeCertWatermarks([]byte(tc.input))
			if tc.wantErr {
				if err == nil {
					t.Fatal("expected error, got nil")
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if len(got) != tc.wantLen {
				t.Errorf("got %d entries, want %d", len(got), tc.wantLen)
			}
			for cn, ts := range got {
				if ts.Location() != time.UTC {
					t.Errorf("watermark for %q is not UTC: %v", cn, ts)
				}
			}
		})
	}
}

func TestSecurity_CertWatermarks_RoundTrip(t *testing.T) {
	for name, tc := range map[string]struct {
		input CertWatermarks
	}{
		"single node": {input: CertWatermarks{
			"node:node1": time.Date(2026, 4, 15, 14, 0, 29, 0, time.UTC),
		}},
		"multiple": {input: CertWatermarks{
			"node:node1":   time.Date(2026, 4, 15, 14, 0, 29, 0, time.UTC),
			"node:node2":   time.Date(2026, 4, 15, 14, 0, 30, 0, time.UTC),
			"tenant:teamA": time.Date(2026, 4, 20, 9, 15, 0, 0, time.UTC),
		}},
	} {
		t.Run(name, func(t *testing.T) {
			encoded, err := encodeCertWatermarks(tc.input)
			if err != nil {
				t.Fatalf("encode: %v", err)
			}
			got, err := DecodeCertWatermarks(encoded)
			if err != nil {
				t.Fatalf("decode: %v", err)
			}
			if diff := cmp.Diff(tc.input, got); diff != "" {
				t.Fatalf("round-trip mismatch (-want +got):\n%s", diff)
			}
		})
	}
}

func TestSecurity_advanceCertWatermark(t *testing.T) {
	t0 := time.Date(2026, 4, 15, 14, 0, 0, 0, time.UTC)
	for name, tc := range map[string]struct {
		existing CertWatermarks
		cn       string
		now      time.Time
		want     time.Time
	}{
		"no prior":             {nil, "node:n1", t0.Add(500 * time.Millisecond), t0},
		"no prior for cn":      {CertWatermarks{"node:other": t0.Add(time.Hour)}, "node:n1", t0, t0},
		"now > prior":          {CertWatermarks{"node:n1": t0}, "node:n1", t0.Add(time.Minute), t0.Add(time.Minute)},
		"now == prior":         {CertWatermarks{"node:n1": t0}, "node:n1", t0, t0.Add(time.Second)},
		"now < prior":          {CertWatermarks{"node:n1": t0.Add(time.Hour)}, "node:n1", t0, t0.Add(time.Hour).Add(time.Second)},
		"sub-second truncated": {nil, "node:n1", t0.Add(999 * time.Millisecond), t0},
	} {
		t.Run(name, func(t *testing.T) {
			got := advanceCertWatermark(tc.existing, tc.cn, tc.now)
			if !got.Equal(tc.want) {
				t.Errorf("got %s, want %s", got.Format(time.RFC3339), tc.want.Format(time.RFC3339))
			}
		})
	}
}

func TestSecurity_RemoveCertByFingerprint(t *testing.T) {
	pemA, _ := sectest.NewCA(t, "CA-A", nil, nil)
	pemB, _ := sectest.NewCA(t, "CA-B", nil, nil)
	pemC, _ := sectest.NewCA(t, "CA-C", nil, nil)

	bundle := append(append(pemA, pemB...), pemC...)
	fpA := CertFingerprint(sectest.ParseCert(t, pemA))
	fpB := CertFingerprint(sectest.ParseCert(t, pemB))

	for name, tc := range map[string]struct {
		fp     string
		want   int
		errSub string
	}{
		"remove first":  {fpA, 1, ""},
		"remove middle": {fpB, 1, ""},
		"not found":     {"deadbeef", 0, "no CA with fingerprint"},
	} {
		t.Run(name, func(t *testing.T) {
			_, removed, err := RemoveCertByFingerprint(bundle, tc.fp)
			if tc.errSub != "" {
				if err == nil {
					t.Fatal("expected error")
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if removed != tc.want {
				t.Errorf("removed=%d, want %d", removed, tc.want)
			}
		})
	}
}

func TestSecurity_ParsePoolCACert(t *testing.T) {
	caPEM, _ := sectest.NewCA(t, "valid CA", nil, nil)

	rootPEM, rootKey := sectest.NewCA(t, "root", nil, nil)
	leaf, err := issueClientCert("leaf", sectest.ParseCert(t, rootPEM), rootKey, time.Now(), time.Now().Add(time.Hour))
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		input   []byte
		wantErr string
	}{
		"valid CA":    {caPEM, ""},
		"not a CA":    {leaf.CertPEM, "IsCA"},
		"empty input": {nil, "exactly one CERTIFICATE"},
		"two blocks":  {append(caPEM, caPEM...), "exactly one CERTIFICATE"},
		"truncated":   {caPEM[:len(caPEM)-20], "trailing non-PEM"},
	} {
		t.Run(name, func(t *testing.T) {
			_, err := ParsePoolCACert(tc.input)
			if tc.wantErr != "" {
				if err == nil || !strings.Contains(err.Error(), tc.wantErr) {
					t.Fatalf("expected error containing %q, got %v", tc.wantErr, err)
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
		})
	}
}

func TestSecurity_CheckPoolCAIdentity(t *testing.T) {
	poolUUID := uuid.MustParse("9bd63a8b-1c5f-4c3a-9a63-7f3b1d0e2a11")
	for name, tc := range map[string]struct {
		cn     string
		uuid   uuid.UUID
		expErr string
	}{
		"matches":                 {cn: poolCACommonName(poolUUID), uuid: poolUUID},
		"lowercase CN":            {cn: "DAOS Pool CA 9bd63a8b-1c5f-4c3a-9a63-7f3b1d0e2a11", uuid: poolUUID},
		"another pool":            {cn: poolCACommonName(uuid.Nil), uuid: poolUUID, expErr: "is not for pool"},
		"CN without pool binding": {cn: "Admin CA", uuid: poolUUID, expErr: "is not for pool"},
	} {
		t.Run(name, func(t *testing.T) {
			certPEM, _ := sectest.NewCA(t, tc.cn, nil, nil)
			err := CheckPoolCAIdentity(sectest.ParseCert(t, certPEM), tc.uuid)
			if tc.expErr == "" {
				if err != nil {
					t.Fatalf("unexpected error: %v", err)
				}
				return
			}
			if err == nil || !strings.Contains(err.Error(), tc.expErr) {
				t.Fatalf("expected %q, got %v", tc.expErr, err)
			}
		})
	}
}

func TestSecurity_VerifyPoolCAChain(t *testing.T) {
	rootPEM, rootKey := sectest.NewCA(t, "root", nil, nil)
	root := sectest.ParseCert(t, rootPEM)
	inter, err := GeneratePoolCA(uuid.Nil, root, rootKey, 0)
	if err != nil {
		t.Fatal(err)
	}

	if err := VerifyPoolCAChain(inter.Cert, root); err != nil {
		t.Fatalf("valid chain rejected: %v", err)
	}
	if err := VerifyPoolCAChain(inter.Cert, nil); !errors.Is(err, ErrInvalidInput) {
		t.Fatalf("no root: expected %v, got %v", ErrInvalidInput, err)
	}

	freshRootPEM, freshKey := sectest.NewCA(t, "fresh root", nil, nil)
	bad, err := GeneratePoolCA(uuid.Nil, sectest.ParseCert(t, freshRootPEM), freshKey, 0)
	if err != nil {
		t.Fatal(err)
	}
	if err := VerifyPoolCAChain(bad.Cert, root); err == nil {
		t.Fatal("cert not chaining to root should be rejected")
	}
}

func TestSecurity_ParseCABundle(t *testing.T) {
	pemA, _ := sectest.NewCA(t, "A", nil, nil)
	pemB, _ := sectest.NewCA(t, "B", nil, nil)
	notACert := pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: []byte("x")})
	garbage := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: []byte("not der")})

	for name, tc := range map[string]struct {
		bundle []byte
		expN   int
		expErr bool
	}{
		"empty":                 {bundle: nil},
		"one":                   {bundle: pemA, expN: 1},
		"two":                   {bundle: append(append([]byte{}, pemA...), pemB...), expN: 2},
		"non-certificate block": {bundle: append(append([]byte{}, pemA...), notACert...), expErr: true},
		"malformed certificate": {bundle: append(append([]byte{}, pemA...), garbage...), expErr: true},
		"truncated final block": {bundle: append(append([]byte{}, pemA...), pemB[:len(pemB)-20]...), expErr: true},
		"text before a block":   {bundle: append([]byte("subject=CN=A\n"), pemA...), expN: 1},
	} {
		t.Run(name, func(t *testing.T) {
			entries, err := ParseCABundle(tc.bundle)
			if tc.expErr {
				if err == nil {
					t.Fatal("expected error")
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if len(entries) != tc.expN {
				t.Fatalf("got %d entries, want %d", len(entries), tc.expN)
			}
			for _, e := range entries {
				if e.Fingerprint != CertFingerprint(e.Cert) {
					t.Errorf("fingerprint mismatch for %s", e.Cert.Subject.CommonName)
				}
			}
		})
	}
}

func TestSecurity_CertPathHelpers(t *testing.T) {
	for name, tc := range map[string]struct{ got, exp string }{
		"pool CA cert": {func() string { c, _ := PoolCAPaths("/d", uuid.Nil); return c }(), "/d/" + uuid.Nil.String() + "_ca.crt"},
		"pool CA key":  {func() string { _, k := PoolCAPaths("/d", uuid.Nil); return k }(), "/d/" + uuid.Nil.String() + "_ca.key"},
		"pool CA dir":  {DefaultPoolCADir("/etc/daos/certs/admin.key"), "/etc/daos/certs/pools"},
		"DAOS CA key":  {DefaultDAOSCAKeyPath("/etc/daos/certs/daosCA.crt"), "/etc/daos/certs/daosCA.key"},
	} {
		if tc.got != tc.exp {
			t.Errorf("%s: got %q, want %q", name, tc.got, tc.exp)
		}
	}
}
