//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"bytes"
	"crypto/sha256"
	"crypto/x509"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"path/filepath"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"
)

// PoolCAPaths returns the paths of a pool's CA certificate and key in dir.
func PoolCAPaths(dir string, poolUUID uuid.UUID) (certPath, keyPath string) {
	return filepath.Join(dir, poolUUID.String()+"_ca.crt"), filepath.Join(dir, poolUUID.String()+"_ca.key")
}

// DefaultPoolCADir returns the default pool CA directory.
func DefaultPoolCADir(adminKeyPath string) string {
	return filepath.Join(filepath.Dir(adminKeyPath), "pools")
}

// DefaultDAOSCAKeyPath returns the default DAOS CA key path for a DAOS CA certificate.
func DefaultDAOSCAKeyPath(caCertPath string) string {
	return filepath.Join(filepath.Dir(caCertPath), "daosCA.key")
}

// CertWatermarks maps a pool cert CN to the NotBefore at or below which its certs are revoked.
type CertWatermarks map[string]time.Time

// watermarkCutoff returns the earliest time a cert could have been issued under bundle.
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

func pruneCertWatermarks(wm CertWatermarks, cutoff time.Time) CertWatermarks {
	out := make(CertWatermarks, len(wm))
	for cn, t := range wm {
		if !t.Before(cutoff) {
			out[cn] = t
		}
	}
	return out
}

// encodeCertWatermarks serializes watermarks as JSON.
func encodeCertWatermarks(wm CertWatermarks) ([]byte, error) {
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

// advanceCertWatermark returns a watermark for cn later than its existing one.
func advanceCertWatermark(existing CertWatermarks, cn string, now time.Time) time.Time {
	now = now.UTC().Truncate(time.Second)
	if prev, ok := existing[cn]; ok && !now.After(prev) {
		return prev.Add(time.Second)
	}
	return now
}

// RevokeCertWatermark revokes cn's certs issued up to now; it returns the updated blob and the recorded watermark.
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
	encoded, err := encodeCertWatermarks(pruneCertWatermarks(wm, cutoff))
	if err != nil {
		return nil, time.Time{}, errors.Wrap(err, "updated watermarks")
	}
	return encoded, committed, nil
}

// RemoveCertByFingerprint drops the certificate with the SHA-256 hex fingerprint from a PEM bundle.
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

// ParsePoolCACert parses a certificate that may serve as a pool CA.
func ParsePoolCACert(certPEM []byte) (*x509.Certificate, error) {
	entries, err := ParseCABundle(certPEM)
	if err != nil {
		return nil, err
	}
	if len(entries) != 1 {
		return nil, errors.Errorf("pool CA must be exactly one CERTIFICATE block, got %d", len(entries))
	}
	cert := entries[0].Cert
	if !cert.IsCA {
		return nil, errors.New("certificate does not have IsCA=true")
	}
	if cert.KeyUsage&x509.KeyUsageCertSign == 0 {
		return nil, errors.New("certificate KeyUsage does not include KeyCertSign")
	}
	return cert, nil
}

func poolCACommonName(poolUUID uuid.UUID) string {
	return "DAOS Pool CA " + poolUUID.String()
}

// CheckPoolCAIdentity rejects a CA whose CN names a different pool.
func CheckPoolCAIdentity(cert *x509.Certificate, poolUUID uuid.UUID) error {
	if want := poolCACommonName(poolUUID); cert.Subject.CommonName != want {
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

// ParseCABundle parses a PEM bundle of certificates.
func ParseCABundle(bundle []byte) ([]CABundleEntry, error) {
	var out []CABundleEntry
	for rest := bundle; len(rest) > 0; {
		var block *pem.Block
		block, rest = pem.Decode(rest)
		if block == nil {
			if len(bytes.TrimSpace(rest)) > 0 {
				return nil, errors.New("trailing non-PEM data in CA bundle")
			}
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
