//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"crypto/sha256"
	"crypto/x509"
	"encoding/pem"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"time"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/security"
)

// writeFileOverwrite writes data to path, removing any existing file first
// (needed when the existing file has restrictive permissions like 0400).
func writeFileOverwrite(path string, data []byte, perm os.FileMode) error {
	os.Remove(path)
	return os.WriteFile(path, data, perm)
}

// poolCertInfo describes a single CA certificate in JSON output.
type poolCertInfo struct {
	Subject     string `json:"subject"`
	Issuer      string `json:"issuer"`
	NotBefore   string `json:"not_before"`
	NotAfter    string `json:"not_after"`
	Fingerprint string `json:"fingerprint"`
}

func parseCertBundle(pemData []byte) ([]poolCertInfo, error) {
	var certs []poolCertInfo
	rest := pemData
	for len(rest) > 0 {
		var block *pem.Block
		block, rest = pem.Decode(rest)
		if block == nil {
			break
		}
		cert, err := x509.ParseCertificate(block.Bytes)
		if err != nil {
			continue
		}
		fingerprint := sha256.Sum256(cert.Raw)
		certs = append(certs, poolCertInfo{
			Subject:     cert.Subject.String(),
			Issuer:      cert.Issuer.String(),
			NotBefore:   cert.NotBefore.Format(time.RFC3339),
			NotAfter:    cert.NotAfter.Format(time.RFC3339),
			Fingerprint: fmt.Sprintf("%x", fingerprint),
		})
	}
	return certs, nil
}

// poolSetCertCmd sets a pool-specific intermediate CA certificate.
type poolSetCertCmd struct {
	poolCmd
	CAKey  string `long:"daos-ca-key" description:"Path to DAOS CA private key"`
	Output string `long:"output" short:"o" description:"Output directory for pool CA key pair"`
	Cert   string `long:"cert" description:"Path to pre-existing pool CA certificate (import mode)"`
}

func (cmd *poolSetCertCmd) Execute(args []string) error {
	req := &control.PoolSetupCertAuthReq{
		ID: cmd.PoolID().String(),
	}

	if cmd.Cert != "" {
		// Import mode
		if cmd.CAKey != "" || cmd.Output != "" {
			return errors.New("--cert is mutually exclusive with --daos-ca-key and --output")
		}

		certPEM, err := os.ReadFile(cmd.Cert)
		if err != nil {
			return errors.Wrap(err, "reading pool CA certificate")
		}

		req.CertPEM = certPEM
		// Chain validation requires the DAOS CA cert on disk.
		// Skip when running insecure (no certs available).
		if cmd.config == nil || cmd.config.TransportConfig == nil ||
			!cmd.config.TransportConfig.AllowInsecure {
			req.DaosCACertPath = cmd.getDaosCACertPath()
		}
	} else {
		// Generate mode
		if cmd.CAKey == "" || cmd.Output == "" {
			return errors.New("--daos-ca-key and --output are required (or use --cert for import mode)")
		}

		req.DaosCACertPath = cmd.getDaosCACertPath()
		req.DaosCAKeyPath = cmd.CAKey
	}

	resp, err := control.PoolSetupCertAuth(cmd.MustLogCtx(), cmd.ctlInvoker, req)
	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, err)
	}
	if err != nil {
		return err
	}

	// Write generated files to output directory (generate mode only).
	if cmd.Output != "" && len(resp.CAKeyPEM) > 0 {
		if err := os.MkdirAll(cmd.Output, 0750); err != nil {
			return errors.Wrap(err, "creating output directory")
		}

		poolResp, err := control.PoolQuery(cmd.MustLogCtx(), cmd.ctlInvoker,
			&control.PoolQueryReq{ID: cmd.PoolID().String()})
		if err != nil {
			return errors.Wrap(err, "resolving pool UUID for output files")
		}
		uuid := poolResp.UUID.String()

		certPath := filepath.Join(cmd.Output, uuid+"_ca.crt")
		if err := writeFileOverwrite(certPath, resp.CACertPEM, 0644); err != nil {
			return errors.Wrap(err, "writing pool CA certificate")
		}
		keyPath := filepath.Join(cmd.Output, uuid+"_ca.key")
		if err := writeFileOverwrite(keyPath, resp.CAKeyPEM, 0400); err != nil {
			return errors.Wrap(err, "writing pool CA key")
		}
		cmd.Infof("Pool CA written to %s and %s", certPath, keyPath)
	}

	cmd.Infof("Pool CA set successfully")
	return nil
}

// getDaosCACertPath returns the DAOS CA cert path from transport config,
// falling back to the default path.
func (cmd *poolSetCertCmd) getDaosCACertPath() string {
	if cmd.config != nil && cmd.config.TransportConfig != nil &&
		cmd.config.TransportConfig.CARootPath != "" {
		return cmd.config.TransportConfig.CARootPath
	}
	return security.DefaultCACertPath
}

// poolGetCertCmd displays the pool's node certificate CA info.
type poolGetCertCmd struct {
	poolCmd
}

func (cmd *poolGetCertCmd) Execute(args []string) error {
	pemStr, err := control.PoolGetCA(cmd.MustLogCtx(), cmd.ctlInvoker,
		&control.PoolGetCAReq{ID: cmd.PoolID().String()})

	type getCertResult struct {
		Certificates []poolCertInfo `json:"certificates"`
	}
	var result getCertResult
	if err == nil && pemStr != "" {
		result.Certificates, _ = parseCertBundle([]byte(pemStr))
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(result, err)
	}
	if err != nil {
		return errors.Wrap(err, "getting pool CA")
	}

	if len(result.Certificates) == 0 {
		cmd.Infof("No pool CA configured")
		return nil
	}

	for i, ci := range result.Certificates {
		cmd.Infof("Pool CA Certificate [%d]:", i)
		cmd.Infof("  Subject:     %s", ci.Subject)
		cmd.Infof("  Issuer:      %s", ci.Issuer)
		cmd.Infof("  Not Before:  %s", ci.NotBefore)
		cmd.Infof("  Not After:   %s", ci.NotAfter)
		cmd.Infof("  Fingerprint: %s", ci.Fingerprint)
	}

	return nil
}

// poolDeleteCertCmd removes CA(s) from the pool's CA bundle.
type poolDeleteCertCmd struct {
	poolCmd
	Fingerprint string `long:"fingerprint" description:"SHA-256 fingerprint of CA to remove (hex)"`
	All         bool   `long:"all" description:"Remove all CAs (disables node auth)"`
}

func (cmd *poolDeleteCertCmd) Execute(args []string) error {
	if !cmd.All && cmd.Fingerprint == "" {
		return errors.New("specify --fingerprint <hex> to remove a specific CA, or --all to remove all")
	}

	req := &control.PoolRemoveCAReq{
		ID: cmd.PoolID().String(),
	}
	if cmd.All {
		req.All = true
	} else {
		req.Fingerprint = cmd.Fingerprint
	}

	resp, err := control.PoolRemoveCA(cmd.MustLogCtx(), cmd.ctlInvoker, req)
	if err != nil {
		return err
	}

	type deleteCertResult struct {
		CertsRemoved int    `json:"certs_removed"`
		Status       string `json:"status"`
	}
	result := deleteCertResult{CertsRemoved: resp.CertsRemoved}
	if cmd.All {
		result.Status = "All pool CAs removed; node auth disabled"
	} else {
		result.Status = fmt.Sprintf("Removed %d CA(s) from bundle", resp.CertsRemoved)
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(result, nil)
	}

	cmd.Infof(result.Status)
	return nil
}

// poolAddClientCmd generates certificates signed by a pool's CA.
// Either --node or --tenant must be specified, but not both.
type poolAddClientCmd struct {
	poolCmd
	CAKey   string   `long:"pool-ca-key" required:"1" description:"Path to pool CA private key"`
	Nodes   []string `long:"node" description:"Node name(s) to generate certs for"`
	Tenants []string `long:"tenant" description:"Tenant name(s) to generate certs for"`
	Output  string   `long:"output" short:"o" required:"1" description:"Output directory"`
}

func (cmd *poolAddClientCmd) Execute(args []string) error {
	poolResp, err := control.PoolQuery(cmd.MustLogCtx(), cmd.ctlInvoker,
		&control.PoolQueryReq{ID: cmd.PoolID().String()})
	if err != nil {
		return errors.Wrap(err, "resolving pool UUID")
	}
	poolUUID := poolResp.UUID.String()

	certs, err := control.PoolGenerateClientCerts(
		&control.PoolGenerateClientCertsReq{
			CAKeyPath: cmd.CAKey,
			Nodes:     cmd.Nodes,
			Tenants:   cmd.Tenants,
		})
	if err != nil {
		return err
	}

	// 0700: these directories hold private CA and/or node cert keys.
	// No group/other access is appropriate.
	if err := os.MkdirAll(cmd.Output, 0700); err != nil {
		return errors.Wrap(err, "creating output directory")
	}

	type clientCertResult struct {
		CN       string `json:"cn"`
		CertPath string `json:"cert_path"`
		KeyPath  string `json:"key_path"`
	}
	var results []clientCertResult

	for _, c := range certs {
		outDir := filepath.Join(cmd.Output, c.Name)
		if err := os.MkdirAll(outDir, 0700); err != nil {
			return errors.Wrapf(err, "creating directory for %s", c.CN)
		}

		r := clientCertResult{CN: c.CN}
		r.CertPath = filepath.Join(outDir, poolUUID+".crt")
		if err := writeFileOverwrite(r.CertPath, c.CertPEM, 0644); err != nil {
			return errors.Wrapf(err, "writing cert for %s", c.CN)
		}
		r.KeyPath = filepath.Join(outDir, poolUUID+".key")
		if err := writeFileOverwrite(r.KeyPath, c.KeyPEM, 0400); err != nil {
			return errors.Wrapf(err, "writing key for %s", c.CN)
		}
		results = append(results, r)
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(results, nil)
	}

	for _, r := range results {
		cmd.Infof("  %s: %s, %s", r.CN, r.CertPath, r.KeyPath)
	}
	kind := "node"
	count := len(cmd.Nodes)
	if len(cmd.Tenants) > 0 {
		kind = "tenant"
		count = len(cmd.Tenants)
	}
	cmd.Infof("Certificates generated for %d %s(s)", count, kind)
	return nil
}

// poolRevokeClientCmd revokes a node or tenant by advancing the pool's
// per-CN revocation watermark and issuing a fresh replacement certificate.
// Any previously-issued cert for that CN is rejected on subsequent pool
// connects, while the freshly-issued cert (whose NotBefore is anchored to
// the watermark) is accepted.
type poolRevokeClientCmd struct {
	poolCmd
	CAKey           string `long:"pool-ca-key" required:"1" description:"Path to pool CA private key"`
	Node            string `long:"node" description:"Node name to revoke"`
	Tenant          string `long:"tenant" description:"Tenant name to revoke"`
	Output          string `long:"output" short:"o" required:"1" description:"Output directory for the replacement cert"`
	EvictAllHandles bool   `long:"evict-all-handles" description:"Evict all active pool handles after advancing the watermark"`
}

func (cmd *poolRevokeClientCmd) Execute(args []string) error {
	if cmd.Node == "" && cmd.Tenant == "" {
		return errors.New("specify --node or --tenant")
	}
	if cmd.Node != "" && cmd.Tenant != "" {
		return errors.New("--node and --tenant are mutually exclusive")
	}

	resp, err := control.PoolRevokeClient(cmd.MustLogCtx(), cmd.ctlInvoker,
		&control.PoolRevokeClientReq{
			ID:              cmd.PoolID().String(),
			CAKeyPath:       cmd.CAKey,
			Node:            cmd.Node,
			Tenant:          cmd.Tenant,
			EvictAllHandles: cmd.EvictAllHandles,
		})
	if err != nil {
		return errors.Wrap(err, "revoking client")
	}

	type revokeResult struct {
		CN             string `json:"cn"`
		CertPath       string `json:"cert_path"`
		KeyPath        string `json:"key_path"`
		Watermark      string `json:"watermark"`
		HandlesEvicted bool   `json:"handles_evicted"`
	}
	result := revokeResult{
		CN:             resp.Cert.CN,
		Watermark:      resp.Watermark.Format(time.RFC3339),
		HandlesEvicted: resp.HandlesEvicted,
	}

	poolResp, err := control.PoolQuery(cmd.MustLogCtx(), cmd.ctlInvoker,
		&control.PoolQueryReq{ID: cmd.PoolID().String()})
	if err != nil {
		return errors.Wrap(err, "resolving pool UUID for output files")
	}
	poolUUID := poolResp.UUID.String()

	outDir := filepath.Join(cmd.Output, resp.Cert.Name)
	if err := os.MkdirAll(outDir, 0700); err != nil {
		return errors.Wrapf(err, "creating output directory for %s", resp.Cert.CN)
	}

	result.CertPath = filepath.Join(outDir, poolUUID+".crt")
	if err := writeFileOverwrite(result.CertPath, resp.Cert.CertPEM, 0644); err != nil {
		return errors.Wrapf(err, "writing cert for %s", resp.Cert.CN)
	}
	result.KeyPath = filepath.Join(outDir, poolUUID+".key")
	if err := writeFileOverwrite(result.KeyPath, resp.Cert.KeyPEM, 0400); err != nil {
		return errors.Wrapf(err, "writing key for %s", resp.Cert.CN)
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(result, nil)
	}

	cmd.Infof("Revoked %s", resp.Cert.CN)
	cmd.Infof("  Watermark:    %s", result.Watermark)
	cmd.Infof("  Replacement:  %s, %s", result.CertPath, result.KeyPath)
	if resp.HandlesEvicted {
		cmd.Infof("  Active handles evicted")
	}
	return nil
}

// poolListRevocationsCmd lists the per-CN revocation watermarks set on a pool.
type poolListRevocationsCmd struct {
	poolCmd
}

func (cmd *poolListRevocationsCmd) Execute(args []string) error {
	entries, err := control.PoolGetCertWatermarks(cmd.MustLogCtx(), cmd.ctlInvoker,
		&control.PoolGetCertWatermarksReq{ID: cmd.PoolID().String()})
	if err != nil {
		return errors.Wrap(err, "reading cert watermarks")
	}

	type listRevocationsResult struct {
		Revocations map[string]string `json:"revocations"`
	}
	result := listRevocationsResult{Revocations: make(map[string]string, len(entries))}
	for cn, wm := range entries {
		result.Revocations[cn] = wm.Format(time.RFC3339)
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(result, nil)
	}

	if len(entries) == 0 {
		cmd.Infof("No revocations set on this pool")
		return nil
	}

	// Print in sorted CN order for stable output.
	cns := make([]string, 0, len(entries))
	for cn := range entries {
		cns = append(cns, cn)
	}
	sort.Strings(cns)

	cmd.Infof("Revocations:")
	for _, cn := range cns {
		cmd.Infof("  %s  %s", cn, entries[cn].Format(time.RFC3339))
	}
	return nil
}
