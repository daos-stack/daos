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

// CertWatermarks maps a pool cert CN to a NotBefore boundary. Certs whose
// NotBefore <= watermark are considered revoked; reissue must produce a
// cert with NotBefore strictly past the watermark.
type CertWatermarks map[string]time.Time

// CertWatermarkRetention bounds the watermark history. An entry older than
// retention can be pruned because any cert covered by it has long since
// passed its NotAfter and would be rejected by the standard validity check.
// Admins issuing certs with validity greater than this must bump it.
const CertWatermarkRetention = 5 * 365 * 24 * time.Hour

// PruneCertWatermarks returns wm minus entries with watermark before cutoff,
// along with the number of entries dropped.
func PruneCertWatermarks(wm CertWatermarks, cutoff time.Time) (CertWatermarks, int) {
	if len(wm) == 0 {
		return wm, 0
	}
	out := make(CertWatermarks, len(wm))
	pruned := 0
	for cn, t := range wm {
		if t.Before(cutoff) {
			pruned++
			continue
		}
		out[cn] = t
	}
	return out, pruned
}

// EncodeCertWatermarks serializes wm as JSON. An empty map encodes to nil
// so the prop layer can distinguish "no revocations" from an empty blob.
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

// DecodeCertWatermarks parses the JSON watermarks blob.
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

// AdvanceCertWatermark returns a value strictly greater than the existing
// watermark for cn. Monotonicity is mandatory: lowering a watermark would
// silently re-validate an already-revoked cert.
func AdvanceCertWatermark(existing CertWatermarks, cn string, now time.Time) time.Time {
	now = now.UTC().Truncate(time.Second)
	if prev, ok := existing[cn]; ok && !now.After(prev) {
		return prev.Add(time.Second)
	}
	return now
}

// RemoveCertByFingerprint drops PEM certs matching a SHA-256 hex fingerprint.
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
		if block.Type != "CERTIFICATE" {
			return nil, 0, fmt.Errorf("unexpected PEM block type %q in CA bundle", block.Type)
		}
		cert, err := x509.ParseCertificate(block.Bytes)
		if err != nil {
			return nil, 0, errors.Wrap(err, "malformed cert in CA bundle")
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

// CountPEMCerts returns the number of CERTIFICATE blocks in a PEM bundle.
func CountPEMCerts(bundle []byte) int {
	count := 0
	rest := bundle
	for len(rest) > 0 {
		var block *pem.Block
		block, rest = pem.Decode(rest)
		if block == nil {
			break
		}
		if block.Type == "CERTIFICATE" {
			count++
		}
	}
	return count
}

// ParsePoolCACert parses a single-block CA cert and enforces IsCA + KeyCertSign.
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

// VerifyPoolCAChain confirms cert chains to the DAOS root at rootCAPath.
// Empty rootCAPath skips verification (AllowInsecure mode).
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

	// ExtKeyUsageAny: the DAOS CA is not EKU-constrained; care only about chain validity.
	_, err = cert.Verify(x509.VerifyOptions{
		Roots:     pool,
		KeyUsages: []x509.ExtKeyUsage{x509.ExtKeyUsageAny},
	})
	if err != nil {
		return errors.Wrap(err, "pool CA does not chain to DAOS CA")
	}
	return nil
}

// AppendCACert validates certPEM as a pool CA and appends it to the bundle.
func AppendCACert(existing, certPEM []byte) ([]byte, error) {
	if _, err := ParsePoolCACert(certPEM); err != nil {
		return nil, err
	}
	combined := make([]byte, 0, len(existing)+len(certPEM))
	combined = append(combined, existing...)
	combined = append(combined, certPEM...)
	return combined, nil
}

// PoolCertInfo summarizes a single PEM CA certificate.
type PoolCertInfo struct {
	Subject     string `json:"subject"`
	Issuer      string `json:"issuer"`
	NotBefore   string `json:"not_before"`
	NotAfter    string `json:"not_after"`
	Fingerprint string `json:"fingerprint"`
}

// ParseCABundle decodes a PEM bundle into a slice of PoolCertInfo entries.
func ParseCABundle(bundle []byte) ([]PoolCertInfo, error) {
	var out []PoolCertInfo
	rest := bundle
	for len(rest) > 0 {
		var block *pem.Block
		block, rest = pem.Decode(rest)
		if block == nil {
			break
		}
		if block.Type != "CERTIFICATE" {
			return nil, fmt.Errorf("unexpected PEM block type %q in CA bundle", block.Type)
		}
		cert, err := x509.ParseCertificate(block.Bytes)
		if err != nil {
			return nil, errors.Wrap(err, "malformed cert in CA bundle")
		}
		fp := sha256.Sum256(cert.Raw)
		out = append(out, PoolCertInfo{
			Subject:     cert.Subject.String(),
			Issuer:      cert.Issuer.String(),
			NotBefore:   cert.NotBefore.Format(time.RFC3339),
			NotAfter:    cert.NotAfter.Format(time.RFC3339),
			Fingerprint: fmt.Sprintf("%x", fp),
		})
	}
	return out, nil
}
