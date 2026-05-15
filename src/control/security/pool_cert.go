//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"crypto/sha256"
	"crypto/x509"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"time"

	"github.com/pkg/errors"
)

// CertWatermarks is the decoded form of the DAOS_PROP_PO_CERT_WATERMARKS
// pool property: a map from full cert CN (including "node:" or "tenant:"
// prefix) to the earliest NotBefore still considered valid for that
// identity. A cert whose NotBefore is strictly less than the watermark
// for its CN is revoked.
type CertWatermarks map[string]time.Time

// EncodeCertWatermarks serializes the map as a flat JSON object keyed by
// CN with RFC3339 UTC timestamp values. The result is what lives in the
// DAOS_PROP_PO_CERT_WATERMARKS pool property. An empty map encodes to
// nil so the property layer can distinguish "no revocations" from an
// empty-but-present blob.
func EncodeCertWatermarks(wm CertWatermarks) ([]byte, error) {
	if len(wm) == 0 {
		return nil, nil
	}
	raw := make(map[string]string, len(wm))
	for cn, t := range wm {
		raw[cn] = t.UTC().Format(time.RFC3339)
	}
	return json.Marshal(raw)
}

// DecodeCertWatermarks parses the flat JSON map stored in the cert
// watermarks pool property into a CN-keyed CertWatermarks.
func DecodeCertWatermarks(data []byte) (CertWatermarks, error) {
	raw := make(map[string]string)
	if err := json.Unmarshal(data, &raw); err != nil {
		return nil, errors.Wrap(err, "parsing cert watermarks blob")
	}
	out := make(CertWatermarks, len(raw))
	for cn, ts := range raw {
		t, err := time.Parse(time.RFC3339, ts)
		if err != nil {
			return nil, errors.Wrapf(err, "parsing watermark for %s", cn)
		}
		out[cn] = t.UTC()
	}
	return out, nil
}

// AdvanceCertWatermark returns a watermark value for cn that is strictly
// greater than any existing watermark for the same CN. This is the
// monotonicity invariant: a revocation must never lower a previously-
// committed watermark, because doing so would silently re-validate a
// cert that has already been revoked.
//
// now() is an injected parameter so callers can pin the wall clock in
// tests. Results are UTC and truncated to second precision so they
// compare cleanly against an x509.Certificate.NotBefore (which is
// serialized at second granularity).
func AdvanceCertWatermark(existing CertWatermarks, cn string, now time.Time) time.Time {
	now = now.UTC().Truncate(time.Second)
	if prev, ok := existing[cn]; ok && !now.After(prev) {
		return prev.Add(time.Second)
	}
	return now
}

// RemoveCertByFingerprint removes a PEM certificate matching the given
// SHA-256 hex fingerprint from a PEM bundle. Returns the remaining
// bundle and the number of certs removed. If no matching cert is found,
// returns an error.
//
// Unparseable PEM blocks are preserved in the output bundle so this
// helper never loses data it does not recognize.
func RemoveCertByFingerprint(bundle []byte, fingerprint string) ([]byte, int, error) {
	var remaining []byte
	removed := 0
	rest := bundle

	for len(rest) > 0 {
		var block *pem.Block
		block, rest = pem.Decode(rest)
		if block == nil {
			break
		}
		cert, err := x509.ParseCertificate(block.Bytes)
		if err != nil {
			remaining = append(remaining, pem.EncodeToMemory(block)...)
			continue
		}
		fp := fmt.Sprintf("%x", sha256.Sum256(cert.Raw))
		if fp == fingerprint {
			removed++
			continue
		}
		remaining = append(remaining, pem.EncodeToMemory(block)...)
	}

	if removed == 0 {
		return nil, 0, fmt.Errorf("no CA with fingerprint %s found in bundle", fingerprint)
	}

	return remaining, removed, nil
}

// ParsePoolCACert decodes certPEM and enforces the local (chain-independent)
// rules for a pool intermediate CA: exactly one PEM CERTIFICATE block,
// IsCA=true, and KeyUsage includes KeyCertSign.
func ParsePoolCACert(certPEM []byte) (*x509.Certificate, error) {
	block, rest := pem.Decode(certPEM)
	if block == nil || block.Type != "CERTIFICATE" {
		return nil, errors.New("cert_pem is not a PEM-encoded CERTIFICATE block")
	}
	if len(rest) > 0 {
		trailing, _ := pem.Decode(rest)
		if trailing != nil {
			return nil, errors.New("cert_pem must contain exactly one CERTIFICATE block")
		}
	}
	cert, err := x509.ParseCertificate(block.Bytes)
	if err != nil {
		return nil, errors.Wrap(err, "parsing CA certificate")
	}
	if !cert.IsCA {
		return nil, errors.New("certificate does not have IsCA=true")
	}
	if cert.KeyUsage&x509.KeyUsageCertSign == 0 {
		return nil, errors.New("certificate KeyUsage does not include KeyCertSign")
	}
	return cert, nil
}

// VerifyPoolCAChain confirms that cert chains to the DAOS root CA at
// rootCAPath. Skipped by callers in AllowInsecure mode (empty rootCAPath).
func VerifyPoolCAChain(cert *x509.Certificate, rootCAPath string) error {
	if rootCAPath == "" {
		return nil
	}
	root, err := LoadCertificate(rootCAPath)
	if err != nil {
		return errors.Wrap(err, "loading DAOS CA certificate")
	}
	pool := x509.NewCertPool()
	pool.AddCert(root)

	// ExtKeyUsageAny — the DAOS CA is a generic root, not an EKU-constrained
	// issuer; we care about chain validity, not leaf-EKU compatibility here.
	_, err = cert.Verify(x509.VerifyOptions{
		Roots:     pool,
		KeyUsages: []x509.ExtKeyUsage{x509.ExtKeyUsageAny},
	})
	if err != nil {
		return errors.Wrap(err, "pool CA does not chain to DAOS CA")
	}
	return nil
}

// AppendCACert returns the bundle with certPEM appended after validating
// certPEM as a pool intermediate CA. The server uses this as the
// read-modify-write body of the PoolAddCA handler.
func AppendCACert(existing, certPEM []byte) ([]byte, error) {
	if _, err := ParsePoolCACert(certPEM); err != nil {
		return nil, err
	}
	combined := make([]byte, 0, len(existing)+len(certPEM))
	combined = append(combined, existing...)
	combined = append(combined, certPEM...)
	return combined, nil
}
