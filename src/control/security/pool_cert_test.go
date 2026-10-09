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

func TestDecodeCertWatermarks(t *testing.T) {
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

func TestCertWatermarks_RoundTrip(t *testing.T) {
	for name, tc := range map[string]struct {
		input CertWatermarks
	}{
		"empty": {input: nil},
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
			encoded, err := EncodeCertWatermarks(tc.input)
			if err != nil {
				t.Fatalf("encode: %v", err)
			}
			if len(tc.input) == 0 {
				if encoded != nil {
					t.Fatalf("empty map must encode to nil, got %q", encoded)
				}
				return
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

func TestAdvanceCertWatermark(t *testing.T) {
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

func TestWatermarkCutoff(t *testing.T) {
	olderPEM, _ := sectest.NewCA(t, "older", nil, nil)
	older := sectest.ParseCert(t, olderPEM)
	newerPEM, _ := sectest.NewCA(t, "newer", nil, nil)
	for name, tc := range map[string]struct {
		bundle []byte
		exp    time.Time
		expErr bool
	}{
		"empty bundle prunes nothing": {bundle: nil, exp: time.Time{}},
		"single CA":                   {bundle: olderPEM, exp: older.NotBefore},
		"earliest of two":             {bundle: append(append([]byte{}, newerPEM...), olderPEM...), exp: older.NotBefore},
		"malformed CA":                {bundle: pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: []byte("junk")}), expErr: true},
	} {
		t.Run(name, func(t *testing.T) {
			got, err := watermarkCutoff(tc.bundle)
			if tc.expErr {
				if err == nil {
					t.Fatal("expected error")
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if !got.Equal(tc.exp) {
				t.Fatalf("got %s, want %s", got, tc.exp)
			}
		})
	}
}

func TestPruneCertWatermarks(t *testing.T) {
	t0 := time.Date(2026, 4, 15, 14, 0, 0, 0, time.UTC)
	wm := CertWatermarks{
		"node:fresh":    t0,
		"node:stale":    t0.Add(-25 * time.Hour),
		"node:boundary": t0.Add(-24 * time.Hour), // exactly at cutoff stays
	}
	out := pruneCertWatermarks(wm, t0.Add(-24*time.Hour))
	if len(out) != 2 {
		t.Fatalf("len=%d, want 2", len(out))
	}
	if _, ok := out["node:stale"]; ok {
		t.Errorf("stale entry survived prune")
	}
	if _, ok := out["node:fresh"]; !ok {
		t.Errorf("fresh entry was pruned")
	}
	if _, ok := out["node:boundary"]; !ok {
		t.Errorf("boundary entry (==cutoff) was pruned")
	}
}

func TestSecurity_RevokeCertWatermark(t *testing.T) {
	caPEM, _ := sectest.NewCA(t, "CA", nil, nil)
	caNotBefore := sectest.ParseCert(t, caPEM).NotBefore
	now := time.Now().UTC().Truncate(time.Second)
	encode := func(t *testing.T, wm CertWatermarks) []byte {
		t.Helper()
		b, err := EncodeCertWatermarks(wm)
		if err != nil {
			t.Fatal(err)
		}
		return b
	}

	for name, tc := range map[string]struct {
		watermarks   []byte
		bundle       []byte
		expCommitted time.Time
		expWM        CertWatermarks
		expErr       bool
	}{
		"first revocation": {
			bundle:       caPEM,
			expCommitted: now,
			expWM:        CertWatermarks{"node:n1": now},
		},
		"same second bumps past the previous": {
			watermarks:   encode(t, CertWatermarks{"node:n1": now}),
			bundle:       caPEM,
			expCommitted: now.Add(time.Second),
			expWM:        CertWatermarks{"node:n1": now.Add(time.Second)},
		},
		"watermark no CA can enforce is dropped": {
			watermarks:   encode(t, CertWatermarks{"node:old": caNotBefore.Add(-time.Hour), "node:kept": now}),
			bundle:       caPEM,
			expCommitted: now,
			expWM:        CertWatermarks{"node:kept": now, "node:n1": now},
		},
		"unreadable watermarks": {watermarks: []byte("junk"), bundle: caPEM, expErr: true},
		"unreadable bundle": {
			bundle: pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: []byte("junk")}),
			expErr: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			encoded, committed, err := RevokeCertWatermark(tc.watermarks, tc.bundle, "node:n1", now)
			if tc.expErr {
				if err == nil {
					t.Fatal("expected error")
				}
				return
			}
			if err != nil {
				t.Fatal(err)
			}
			if !committed.Equal(tc.expCommitted) {
				t.Errorf("committed %s, want %s", committed, tc.expCommitted)
			}
			got, err := DecodeCertWatermarks(encoded)
			if err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(tc.expWM, got); diff != "" {
				t.Errorf("watermarks (-want, +got):\n%s", diff)
			}
		})
	}
}

func TestRemoveCertByFingerprint(t *testing.T) {
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

func TestRemoveCertByFingerprint_MalformedBundle(t *testing.T) {
	validCA, _ := sectest.NewCA(t, "valid", nil, nil)

	// Non-CERTIFICATE PEM block in bundle is rejected.
	notACert := pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: []byte("x")})
	if _, _, err := RemoveCertByFingerprint(append(validCA, notACert...), "anything"); err == nil {
		t.Fatal("expected error for non-CERTIFICATE PEM block")
	}

	// CERTIFICATE block with garbage bytes is rejected.
	garbage := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: []byte("not der")})
	if _, _, err := RemoveCertByFingerprint(append(validCA, garbage...), "anything"); err == nil {
		t.Fatal("expected error for malformed CERTIFICATE block")
	}
}

func TestParsePoolCACert(t *testing.T) {
	caPEM, _ := sectest.NewCA(t, "valid CA", nil, nil)

	// Build a non-CA cert (leaf) for the IsCA-false case.
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
		"empty input": {nil, "PEM-encoded CERTIFICATE"},
		"two blocks":  {append(caPEM, caPEM...), "exactly one CERTIFICATE"},
	} {
		t.Run(name, func(t *testing.T) {
			_, err := ParsePoolCACert(tc.input)
			if tc.wantErr != "" {
				if err == nil {
					t.Fatal("expected error")
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
		"matches":                 {cn: PoolCACommonName(poolUUID), uuid: poolUUID},
		"lowercase CN":            {cn: "DAOS Pool CA 9bd63a8b-1c5f-4c3a-9a63-7f3b1d0e2a11", uuid: poolUUID},
		"another pool":            {cn: PoolCACommonName(uuid.Nil), uuid: poolUUID, expErr: "is not for pool"},
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

func TestParseCABundle(t *testing.T) {
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
