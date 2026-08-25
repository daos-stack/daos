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

	"github.com/google/uuid"
	"github.com/pkg/errors"
)

// CertWatermarks maps a pool cert CN to a NotBefore boundary. Certs whose
// NotBefore <= watermark are considered revoked.
type CertWatermarks map[string]time.Time

// watermarkCutoff returns the earliest NotBefore among the CAs in bundle.
// This is the cutoff for pruning watermarks: any watermark before this
// cutoff is for a CA that is no longer in the bundle and can be dropped.
func watermarkCutoff(bundle []byte) (time.Time, error) {
	entries, err := ParseCABundle(bundle)
	if err != nil {
		return time.Time{}, err
	}
	var cutoff time.Time
	for _, e := range entries {
		if cutoff.IsZero() || e.Cert.NotBefore.Before(cutoff) {
			cutoff = e.Cert.NotBefore
		}
	}
	return cutoff, nil
}

// pruneCertWatermarks returns wm minus entries with watermark before cutoff.
func pruneCertWatermarks(wm CertWatermarks, cutoff time.Time) CertWatermarks {
	out := make(CertWatermarks, len(wm))
	for cn, t := range wm {
		if !t.Before(cutoff) {
			out[cn] = t
		}
	}
	return out
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

// advanceCertWatermark returns a value strictly greater than the existing
// watermark for cn.
func advanceCertWatermark(existing CertWatermarks, cn string, now time.Time) time.Time {
	now = now.UTC().Truncate(time.Second)
	if prev, ok := existing[cn]; ok && !now.After(prev) {
		return prev.Add(time.Second)
	}
	return now
}

// RevokeCertWatermark returns the pool's watermarks with cn revoked as of
// now, dropping any that no CA in bundle can still enforce, and the
// committed watermark for the reply.
func RevokeCertWatermark(watermarks, bundle []byte, cn string, now time.Time) ([]byte, time.Time, error) {
	wm := CertWatermarks{}
	if len(watermarks) > 0 {
		var err error
		if wm, err = DecodeCertWatermarks(watermarks); err != nil {
			return nil, time.Time{}, errors.Wrap(err, "current watermarks")
		}
	}
	committed := advanceCertWatermark(wm, cn, now)
	wm[cn] = committed
	cutoff, err := watermarkCutoff(bundle)
	if err != nil {
		return nil, time.Time{}, errors.Wrap(err, "pool CA bundle")
	}
	encoded, err := EncodeCertWatermarks(pruneCertWatermarks(wm, cutoff))
	if err != nil {
		return nil, time.Time{}, errors.Wrap(err, "updated watermarks")
	}
	return encoded, committed, nil
}

// RemoveCertByFingerprint drops the certificate with the given SHA-256 hex
// fingerprint from a PEM bundle.
func RemoveCertByFingerprint(bundle []byte, fingerprint string) ([]byte, int, error) {
	entries, err := ParseCABundle(bundle)
	if err != nil {
		return nil, 0, err
	}
	var remaining []byte
	removed := 0
	for _, e := range entries {
		if e.Fingerprint == fingerprint {
			removed++
			continue
		}
		remaining = append(remaining, pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: e.Cert.Raw})...)
	}
	if removed == 0 {
		return nil, 0, fmt.Errorf("no CA with fingerprint %s found in bundle", fingerprint)
	}
	return remaining, removed, nil
}

// ParsePoolCACert parses a single-block CA cert and enforces IsCA + KeyCertSign.
func ParsePoolCACert(certPEM []byte) (*x509.Certificate, error) {
	block, rest := pem.Decode(certPEM)
	if block == nil || block.Type != "CERTIFICATE" {
		return nil, errors.New("pool CA is not a PEM-encoded CERTIFICATE block")
	}
	if len(rest) > 0 {
		trailing, _ := pem.Decode(rest)
		if trailing != nil {
			return nil, errors.New("pool CA must be exactly one CERTIFICATE block")
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

// PoolCACommonName returns the CN a pool CA must carry; the CN binds the CA
// to a single pool.
func PoolCACommonName(poolUUID uuid.UUID) string {
	return "DAOS Pool CA " + poolUUID.String()
}

// CheckPoolCAIdentity rejects a CA whose CN names a different pool.
func CheckPoolCAIdentity(cert *x509.Certificate, poolUUID uuid.UUID) error {
	if want := PoolCACommonName(poolUUID); cert.Subject.CommonName != want {
		return errors.Errorf("pool CA %q is not for pool %s", cert.Subject.CommonName, poolUUID)
	}
	return nil
}

// VerifyPoolCAChain confirms cert chains to the DAOS CA.
func VerifyPoolCAChain(cert, root *x509.Certificate) error {
	if root == nil {
		return errors.Wrap(ErrInvalidInput, "no trust anchor")
	}
	pool := x509.NewCertPool()
	pool.AddCert(root)

	// Only the chain to the DAOS CA matters here; usage is enforced at connect.
	_, err := cert.Verify(x509.VerifyOptions{
		Roots:     pool,
		KeyUsages: []x509.ExtKeyUsage{x509.ExtKeyUsageAny},
	})
	if err != nil {
		return errors.Wrap(err, "pool CA does not chain to DAOS CA")
	}
	return nil
}

// PoolCABundleMaxCerts is the most CAs a pool's bundle may hold.
const PoolCABundleMaxCerts = 3

// CABundleEntry is one certificate of a pool CA bundle.
type CABundleEntry struct {
	Cert        *x509.Certificate
	Fingerprint string // SHA-256 of the DER encoding, hex
}

// CertFingerprint returns the SHA-256 hex fingerprint of a certificate.
func CertFingerprint(cert *x509.Certificate) string {
	return fmt.Sprintf("%x", sha256.Sum256(cert.Raw))
}

// ParseCABundle parses a PEM bundle of CA certificates; any block that is
// not a parsable certificate is an error.
func ParseCABundle(bundle []byte) ([]CABundleEntry, error) {
	var out []CABundleEntry
	for rest := bundle; len(rest) > 0; {
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
		out = append(out, CABundleEntry{Cert: cert, Fingerprint: CertFingerprint(cert)})
	}
	return out, nil
}
