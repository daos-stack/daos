//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"crypto/x509"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/security"
)

// poolNodeAuthCmd groups the per-pool node authentication verbs.
type poolNodeAuthCmd struct {
	Enable       poolNodeAuthEnableCmd       `command:"enable" description:"Enable node authentication on a pool"`
	GenerateCA   poolNodeAuthGenerateCACmd   `command:"generate-ca" description:"Generate a pool CA key pair without contacting the system, for enable --cert"`
	Disable      poolNodeAuthDisableCmd      `command:"disable" description:"Disable node authentication on a pool"`
	Status       poolNodeAuthStatusCmd       `command:"status" description:"Show CAs and revocations for a pool"`
	Issue        poolNodeAuthIssueCmd        `command:"issue" description:"Issue node or tenant certificates for a pool"`
	GenerateCert poolNodeAuthGenerateCertCmd `command:"generate-cert" description:"Mint node or tenant certificates without contacting the system"`
	Revoke       poolNodeAuthRevokeCmd       `command:"revoke" description:"Revoke a node or tenant identity"`
	AddCA        poolNodeAuthAddCACmd        `command:"add-ca" description:"Add a CA to the pool's bundle (rotation)"`
	RemoveCA     poolNodeAuthRemoveCACmd     `command:"remove-ca" description:"Remove a CA from the pool's bundle by fingerprint"`
}

func fileExists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

// writeFileOverwrite writes data to path, removing any existing file first
// (needed when the existing file has restrictive permissions like 0400).
func writeFileOverwrite(path string, data []byte, perm os.FileMode) error {
	os.Remove(path)
	return os.WriteFile(path, data, perm)
}

// poolCACmd carries the flags shared by enable and add-ca: a pool CA is
// either generated from the DAOS CA key or imported from an existing PEM.
type poolCACmd struct {
	poolCmd
	CAKey    string       `long:"daos-ca-key" description:"Path to DAOS CA private key (generate mode; default: daosCA.key beside the DAOS CA certificate)"`
	Output   string       `long:"output" short:"o" description:"Directory for the generated pool CA key pair (default: pools/ under the dmg certificate directory)"`
	Cert     string       `long:"cert" description:"Path to pre-existing pool CA certificate (import mode)"`
	Validity validityFlag `long:"validity" description:"Pool CA lifetime, e.g. 90d, 26w, 2y (generate mode; default: until the DAOS CA expires)"`
}

// caNotAfter returns the expiry date of a PEM CA certificate.
func caNotAfter(certPEM []byte) (string, error) {
	cert, err := security.ParsePoolCACert(certPEM)
	if err != nil {
		return "", err
	}
	return cert.NotAfter.Format("2006-01-02"), nil
}

// validityFlag is a certificate lifetime in days, weeks, or years (90d, 26w, 2y).
type validityFlag struct {
	time.Duration
}

func (f *validityFlag) UnmarshalFlag(fv string) error {
	units := map[byte]time.Duration{'d': 24 * time.Hour, 'w': 7 * 24 * time.Hour, 'y': 365 * 24 * time.Hour}
	if len(fv) < 2 {
		return errors.Errorf("invalid lifetime %q: use <n>d, <n>w, or <n>y", fv)
	}
	unit, ok := units[fv[len(fv)-1]]
	n, err := strconv.ParseUint(fv[:len(fv)-1], 10, 32)
	if !ok || err != nil || n == 0 {
		return errors.Errorf("invalid lifetime %q: use <n>d, <n>w, or <n>y", fv)
	}
	f.Duration = time.Duration(n) * unit
	return nil
}

// poolCADir returns the default pool CA key-pair directory: pools/ beside
// the dmg admin key.
func poolCADir(cfg *control.Config) (string, error) {
	if cfg == nil {
		return "", errors.New("no configuration loaded")
	}
	if cfg.TransportConfig == nil || cfg.TransportConfig.PrivateKeyPath == "" {
		return "", errors.New("transport_config.key is not set")
	}
	return security.DefaultPoolCADir(cfg.TransportConfig.PrivateKeyPath), nil
}

// ensureWritableDir creates dir (0700) and verifies a file can be created in it.
func ensureWritableDir(dir string) error {
	if err := os.MkdirAll(dir, 0700); err != nil {
		return err
	}
	probe, err := os.CreateTemp(dir, ".dmg-probe-*")
	if err != nil {
		return err
	}
	probe.Close()
	return os.Remove(probe.Name())
}

// daosCACertPath returns the configured DAOS CA certificate path.
func daosCACertPath(cfg *control.Config) (string, error) {
	if cfg == nil {
		return "", errors.New("no configuration loaded")
	}
	if cfg.TransportConfig == nil || cfg.TransportConfig.CARootPath == "" {
		return "", errors.New("transport_config.ca_cert is not set")
	}
	return cfg.TransportConfig.CARootPath, nil
}

func (cmd *poolCACmd) getDaosCACertPath() (string, error) {
	return daosCACertPath(cmd.config)
}

// writePoolCA writes the pool CA key pair under dir.
func writePoolCA(dir string, ca *control.PoolCA) (certPath, keyPath string, err error) {
	if err := os.MkdirAll(dir, 0700); err != nil {
		return "", "", errors.Wrapf(err, "creating pool CA directory %s (pass --output to use another)", dir)
	}
	certPath, keyPath = security.PoolCAPaths(dir, ca.PoolUUID)
	if err := writeFileOverwrite(keyPath, ca.KeyPEM, 0400); err != nil {
		return "", "", errors.Wrap(err, "writing pool CA key (pass --output to use another directory)")
	}
	if err := writeFileOverwrite(certPath, ca.CertPEM, 0644); err != nil {
		os.Remove(keyPath)
		return "", "", errors.Wrap(err, "writing pool CA certificate")
	}
	return certPath, keyPath, nil
}

// installCA generates (writing the key pair to Output) or imports a pool CA
// and installs it.
func (cmd *poolCACmd) installCA(appendCA, noEvict bool) (*control.PoolSetupCertAuthResp, string, string, error) {
	ctx := cmd.MustLogCtx()
	id := cmd.PoolID().String()

	if cmd.Cert != "" {
		// Import mode
		if cmd.CAKey != "" || cmd.Output != "" {
			return nil, "", "", errors.New("--cert is mutually exclusive with --daos-ca-key and --output")
		}
		certPEM, err := os.ReadFile(cmd.Cert)
		if err != nil {
			return nil, "", "", errors.Wrap(err, "reading pool CA certificate")
		}
		resp, err := control.PoolInstallCA(ctx, cmd.ctlInvoker,
			&control.PoolInstallCAReq{ID: id, CertPEM: certPEM, Append: appendCA, NoEvict: noEvict})
		if err != nil {
			return nil, "", "", err
		}
		return &control.PoolSetupCertAuthResp{PoolUUID: resp.PoolUUID, CACertPEM: certPEM,
			HandlesEvicted: resp.HandlesEvicted}, "", "", nil
	}

	// Generate mode
	daosCACert, err := cmd.getDaosCACertPath()
	if err != nil {
		return nil, "", "", err
	}
	if cmd.CAKey == "" {
		cmd.CAKey = security.DefaultDAOSCAKeyPath(daosCACert)
		if _, sErr := os.Stat(cmd.CAKey); sErr != nil {
			return nil, "", "", errors.Wrapf(sErr,
				"DAOS CA key not found at its default location (beside the DAOS CA certificate); "+
					"pass --daos-ca-key if it is kept elsewhere, or --cert to import a pool CA")
		}
	}
	if cmd.Output == "" {
		dir, err := poolCADir(cmd.config)
		if err != nil {
			return nil, "", "", err
		}
		cmd.Output = dir
	}

	// Refuse before generating so an existing key pair is never overwritten.
	if !appendCA {
		caResp, err := control.PoolGetCA(ctx, cmd.ctlInvoker, &control.PoolGetCAReq{ID: id})
		if err != nil {
			return nil, "", "", errors.Wrap(err, "checking for an installed CA")
		}
		if n := len(caResp.Certs); n > 0 {
			return nil, "", "", errors.Wrapf(control.ErrNodeAuthEnabled, "%d CA(s) installed", n)
		}
	}

	ca, err := control.PoolGenerateCA(ctx, cmd.ctlInvoker, &control.PoolGenerateCAReq{
		ID:             id,
		DaosCACertPath: daosCACert,
		DaosCAKeyPath:  cmd.CAKey,
		Validity:       cmd.Validity.Duration,
	})
	if err != nil {
		return nil, "", "", err
	}

	certPath, keyPath, err := writePoolCA(cmd.Output, ca)
	if err != nil {
		return nil, "", "", err
	}

	resp, err := control.PoolInstallCA(ctx, cmd.ctlInvoker,
		&control.PoolInstallCAReq{ID: id, CertPEM: ca.CertPEM, Append: appendCA, NoEvict: noEvict})
	if err != nil {
		os.Remove(keyPath)
		os.Remove(certPath)
		return nil, "", "", err
	}

	return &control.PoolSetupCertAuthResp{
		PoolUUID:       resp.PoolUUID,
		CACertPEM:      ca.CertPEM,
		CAKeyPEM:       ca.KeyPEM,
		HandlesEvicted: resp.HandlesEvicted,
	}, certPath, keyPath, nil
}

type caResult struct {
	PoolUUID       uuid.UUID `json:"pool_uuid"`
	CertPath       string    `json:"cert_path,omitempty"`
	KeyPath        string    `json:"key_path,omitempty"`
	NotAfter       string    `json:"not_after"`
	HandlesEvicted int32     `json:"handles_evicted"`
}

// poolNodeAuthEnableCmd enables node authentication by installing the
// pool's first CA.
type poolNodeAuthEnableCmd struct {
	poolCACmd
	NoEvict bool `long:"no-evict" description:"Keep existing pool handles open; by default they are evicted so every client proves access"`
}

func (cmd *poolNodeAuthEnableCmd) Execute(args []string) error {
	var result caResult
	var certPath, keyPath string
	err := func() error {
		resp, cp, kp, iErr := cmd.installCA(false, cmd.NoEvict)
		if errors.Is(iErr, control.ErrNodeAuthEnabled) {
			return errors.Wrap(iErr, "use add-ca/remove-ca to rotate, or disable to start over")
		} else if iErr != nil {
			return iErr
		}
		certPath, keyPath = cp, kp
		notAfter, err := caNotAfter(resp.CACertPEM)
		if err != nil {
			return err
		}
		result = caResult{PoolUUID: resp.PoolUUID, CertPath: certPath, KeyPath: keyPath, NotAfter: notAfter,
			HandlesEvicted: resp.HandlesEvicted}
		return nil
	}()

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(result, err)
	}
	if err != nil {
		return err
	}

	if certPath != "" {
		cmd.Infof("Pool CA written to %s and %s", certPath, keyPath)
	}
	cmd.Infof("Node authentication enabled on %s", cmd.PoolID().String())
	if cmd.NoEvict {
		cmd.Infof("Existing handles kept (--no-evict); they need no certificate until they close")
	} else {
		cmd.Infof("Handles evicted: %d", result.HandlesEvicted)
	}
	cmd.Infof("Pool CA valid until %s; rotate before then (add-ca, reissue, remove-ca)", result.NotAfter)
	return nil
}

// poolNodeAuthGenerateCACmd mints a pool CA where the DAOS CA key is kept;
// only the certificate travels to enable --cert.
type poolNodeAuthGenerateCACmd struct {
	baseCmd
	cfgCmd
	cmdutil.JSONOutputCmd

	Args struct {
		Pool PoolID `positional-arg-name:"<pool UUID>" required:"1"`
	} `positional-args:"yes"`
	DaosCACert string       `long:"daos-ca-cert" description:"Path to DAOS CA certificate (default: transport_config.ca_cert)"`
	CAKey      string       `long:"daos-ca-key" description:"Path to DAOS CA private key (default: daosCA.key beside the DAOS CA certificate)"`
	Output     string       `long:"output" short:"o" description:"Directory for the pool CA key pair (default: pools/ under the dmg certificate directory)"`
	Validity   validityFlag `long:"validity" description:"Pool CA lifetime, e.g. 90d, 26w, 2y (default: until the DAOS CA expires)"`
}

func (cmd *poolNodeAuthGenerateCACmd) Execute(args []string) error {
	var result caResult
	err := func() error {
		if !cmd.Args.Pool.HasUUID() {
			return errors.New("generate-ca takes the pool UUID; a label cannot be resolved without the system")
		}
		poolUUID := cmd.Args.Pool.UUID
		if cmd.DaosCACert == "" {
			p, err := daosCACertPath(cmd.config)
			if err != nil {
				return err
			}
			cmd.DaosCACert = p
		}
		if cmd.CAKey == "" {
			cmd.CAKey = security.DefaultDAOSCAKeyPath(cmd.DaosCACert)
		}
		if cmd.Output == "" {
			dir, err := poolCADir(cmd.config)
			if err != nil {
				return err
			}
			cmd.Output = dir
		}
		// Never overwrite a key pair; issue may depend on it.
		if _, keyPath := security.PoolCAPaths(cmd.Output, poolUUID); fileExists(keyPath) {
			return errors.Errorf("pool CA key already exists at %s (pass --output to use another directory)", keyPath)
		}

		ca, err := control.GeneratePoolCA(poolUUID, cmd.DaosCACert, cmd.CAKey, cmd.Validity.Duration)
		if err != nil {
			return err
		}
		certPath, keyPath, err := writePoolCA(cmd.Output, ca)
		if err != nil {
			return err
		}
		result = caResult{PoolUUID: ca.PoolUUID, CertPath: certPath, KeyPath: keyPath,
			NotAfter: ca.NotAfter.Format("2006-01-02")}
		return nil
	}()

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(result, err)
	}
	if err != nil {
		return err
	}
	cmd.Infof("Pool CA written to %s and %s", result.CertPath, result.KeyPath)
	cmd.Infof("Pool CA valid until %s", result.NotAfter)
	cmd.Infof("Install it with: dmg pool node-auth enable %s --cert %s", result.PoolUUID, result.CertPath)
	return nil
}

// poolNodeAuthAddCACmd appends a CA to an existing bundle (rotation).
type poolNodeAuthAddCACmd struct {
	poolCACmd
}

func (cmd *poolNodeAuthAddCACmd) Execute(args []string) error {
	resp, certPath, keyPath, err := cmd.installCA(true, false)

	var result caResult
	if err == nil {
		var notAfter string
		notAfter, err = caNotAfter(resp.CACertPEM)
		result = caResult{PoolUUID: resp.PoolUUID, CertPath: certPath, KeyPath: keyPath, NotAfter: notAfter}
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(result, err)
	}
	if err != nil {
		return err
	}

	if certPath != "" {
		cmd.Infof("Pool CA written to %s and %s", certPath, keyPath)
	}
	cmd.Infof("New pool CA valid until %s", result.NotAfter)
	cmd.Infof("CA added to pool bundle; reissue certificates from the new CA, then remove-ca the old fingerprint")
	return nil
}

// poolNodeAuthDisableCmd removes all CAs, disabling node authentication.
type poolNodeAuthDisableCmd struct {
	poolCmd
}

func (cmd *poolNodeAuthDisableCmd) Execute(args []string) error {
	resp, err := control.PoolRemoveCA(cmd.MustLogCtx(), cmd.ctlInvoker,
		&control.PoolRemoveCAReq{
			ID:  cmd.PoolID().String(),
			All: true,
		})

	type disableResult struct {
		CertsRemoved int    `json:"certs_removed"`
		Status       string `json:"status"`
	}
	var result disableResult
	if err == nil {
		result = disableResult{
			CertsRemoved: resp.CertsRemoved,
			Status:       "node authentication disabled",
		}
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(result, err)
	}
	if err != nil {
		return err
	}

	cmd.Infof("Node authentication disabled on %s (%d CA(s) removed)",
		cmd.PoolID().String(), resp.CertsRemoved)
	return nil
}

// poolNodeAuthRemoveCACmd removes a single CA from the bundle by fingerprint.
type poolNodeAuthRemoveCACmd struct {
	poolCmd
	Fingerprint string `long:"fingerprint" required:"1" description:"SHA-256 fingerprint of CA to remove (hex)"`
}

func (cmd *poolNodeAuthRemoveCACmd) Execute(args []string) error {
	resp, err := control.PoolRemoveCA(cmd.MustLogCtx(), cmd.ctlInvoker,
		&control.PoolRemoveCAReq{
			ID:          cmd.PoolID().String(),
			Fingerprint: cmd.Fingerprint,
		})

	type removeCAResult struct {
		CertsRemoved int `json:"certs_removed"`
	}
	var result removeCAResult
	if err == nil {
		result = removeCAResult{CertsRemoved: resp.CertsRemoved}
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(result, err)
	}
	if err != nil {
		return err
	}

	cmd.Infof("Removed %d CA(s) from bundle; certificates issued by them are no longer accepted",
		resp.CertsRemoved)
	return nil
}

// poolNodeAuthStatusCmd shows the pool's CA bundle and revocations in one view.
type poolNodeAuthStatusCmd struct {
	poolCmd
}

func (cmd *poolNodeAuthStatusCmd) Execute(args []string) error {
	caResp, err := control.PoolGetCA(cmd.MustLogCtx(), cmd.ctlInvoker,
		&control.PoolGetCAReq{ID: cmd.PoolID().String()})
	if err != nil {
		return errors.Wrap(err, "getting pool CA")
	}
	certs := caResp.Certs

	var watermarks security.CertWatermarks
	if len(certs) > 0 {
		watermarks, err = control.PoolGetCertWatermarks(cmd.MustLogCtx(), cmd.ctlInvoker,
			&control.PoolGetCertWatermarksReq{ID: cmd.PoolID().String()})
		if err != nil {
			return errors.Wrap(err, "reading cert watermarks")
		}
	}

	type statusResult struct {
		Enabled      bool                    `json:"enabled"`
		Certificates []security.PoolCertInfo `json:"certificates"`
		Revocations  map[string]string       `json:"revocations"`
	}
	result := statusResult{
		Enabled:      len(certs) > 0,
		Certificates: certs,
		Revocations:  make(map[string]string, len(watermarks)),
	}
	for cn, wm := range watermarks {
		result.Revocations[cn] = wm.Format(time.RFC3339)
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(result, nil)
	}

	if !result.Enabled {
		cmd.Infof("Node authentication: disabled")
		return nil
	}

	cmd.Infof("Node authentication: enabled")
	for i, ci := range certs {
		cmd.Infof("CA Certificate [%d]:", i)
		cmd.Infof("  Subject:     %s", ci.Subject)
		cmd.Infof("  Issuer:      %s", ci.Issuer)
		cmd.Infof("  Not Before:  %s", ci.NotBefore)
		cmd.Infof("  Not After:   %s", ci.NotAfter)
		cmd.Infof("  Fingerprint: %s", ci.Fingerprint)
		notAfter, err := time.Parse(time.RFC3339, ci.NotAfter)
		if err != nil {
			return errors.Wrapf(err, "parsing NotAfter of CA %d", i)
		}
		if w := security.ExpiryWarning("CA", &x509.Certificate{NotAfter: notAfter}, time.Now(), security.CertExpiryWarnWindow); w != "" {
			cmd.Infof("  WARNING:     %s — rotate (add-ca, reissue, remove-ca)", w)
		}
	}

	if len(watermarks) == 0 {
		cmd.Infof("Revocations: none")
		return nil
	}
	cns := make([]string, 0, len(watermarks))
	for cn := range watermarks {
		cns = append(cns, cn)
	}
	sort.Strings(cns)
	cmd.Infof("Revocations:")
	for _, cn := range cns {
		cmd.Infof("  %s  %s", cn, watermarks[cn].Format(time.RFC3339))
	}
	return nil
}

// poolNodeAuthIssueCmd issues certificates signed by the pool's CA for
// --node or --tenant identities.
type poolNodeAuthIssueCmd struct {
	poolCmd
	CAKey    string       `long:"pool-ca-key" description:"Path to pool CA private key (default: <pool_uuid>_ca.key under pools/ in the dmg certificate directory)"`
	Nodes    []string     `long:"node" description:"Node name(s) to generate certs for"`
	Tenants  []string     `long:"tenant" description:"Tenant name(s) to generate certs for"`
	Output   string       `long:"output" short:"o" description:"Directory for the issued certificates, one subdirectory per name (default: <pool_uuid>/ under pools/ in the dmg certificate directory)"`
	Replace  bool         `long:"replace" description:"Rotate: revoke the node's existing certificate before issuing (node certs only)"`
	Validity validityFlag `long:"validity" description:"Certificate lifetime, e.g. 90d, 26w, 2y (default: 1y; never past the pool CA's expiry)"`
}

// stagedNodeCerts returns the requested nodes whose staged certificate is
// still live (not covered by the pool's watermark). Tenants are never reported.
func (cmd *poolNodeAuthIssueCmd) stagedNodeCerts(poolUUID uuid.UUID, watermarks security.CertWatermarks) []string {
	var found []string
	for _, name := range cmd.Nodes {
		path, _ := security.NodeCertPaths(filepath.Join(cmd.Output, name), poolUUID)
		if _, err := os.Stat(path); err != nil {
			continue
		}
		cert, err := security.LoadCertificate(path)
		if err != nil {
			// Present but unreadable: treat as live rather than overwrite it.
			found = append(found, name)
			continue
		}
		if wm, ok := watermarks[security.CertCNPrefixNode+name]; ok && !cert.NotBefore.After(wm) {
			continue // already revoked
		}
		found = append(found, name)
	}
	return found
}

func (cmd *poolNodeAuthIssueCmd) Execute(args []string) error {
	if len(cmd.Nodes) == 0 && len(cmd.Tenants) == 0 {
		return errors.New("specify --node or --tenant")
	}
	if len(cmd.Nodes) > 0 && len(cmd.Tenants) > 0 {
		return errors.New("--node and --tenant are mutually exclusive")
	}
	if cmd.Replace && len(cmd.Tenants) > 0 {
		return errors.New("--replace applies to node certificates only; " +
			"deploy the new tenant certificate first, then revoke the old one")
	}

	poolResp, err := control.PoolQuery(cmd.MustLogCtx(), cmd.ctlInvoker,
		&control.PoolQueryReq{ID: cmd.PoolID().String()})
	if err != nil {
		return errors.Wrap(err, "resolving pool UUID")
	}
	poolUUID := poolResp.UUID

	if cmd.CAKey == "" {
		dir, err := poolCADir(cmd.config)
		if err != nil {
			return err
		}
		_, cmd.CAKey = security.PoolCAPaths(dir, poolUUID)
		if _, sErr := os.Stat(cmd.CAKey); sErr != nil {
			return errors.Wrapf(sErr,
				"pool CA key not found at its default location (written there by node-auth enable on this host); "+
					"pass --pool-ca-key if it is kept elsewhere")
		}
	}

	if cmd.Output == "" {
		dir, err := poolCADir(cmd.config)
		if err != nil {
			return err
		}
		cmd.Output = filepath.Join(dir, poolUUID.String())
	}
	if err := ensureWritableDir(cmd.Output); err != nil {
		return errors.Wrapf(err,
			"certificate directory %s is not writable; fix its permissions or pass --output",
			cmd.Output)
	}

	watermarks, err := control.PoolGetCertWatermarks(cmd.MustLogCtx(), cmd.ctlInvoker,
		&control.PoolGetCertWatermarksReq{ID: cmd.PoolID().String()})
	if err != nil {
		return errors.Wrap(err, "reading cert watermarks")
	}

	// Detection is limited to certificates this host staged into cmd.Output.
	if staged := cmd.stagedNodeCerts(poolUUID, watermarks); len(staged) > 0 && !cmd.Replace {
		return errors.Errorf("certificate already issued for node(s) %s in %s; "+
			"reissuing leaves the existing certificate valid until it expires. "+
			"Pass --replace to revoke it first, or --output to issue alongside it",
			strings.Join(staged, ", "), cmd.Output)
	}

	certs, err := control.PoolIssueClientCerts(cmd.MustLogCtx(), cmd.ctlInvoker,
		&control.PoolIssueClientCertsReq{
			ID:        cmd.PoolID().String(),
			CAKeyPath: cmd.CAKey,
			Validity:  cmd.Validity.Duration,
			Nodes:     cmd.Nodes,
			Tenants:   cmd.Tenants,
			Replace:   cmd.Replace,
		})
	if err != nil {
		return err
	}
	if cmd.Replace {
		cmd.Infof("Revoked the existing certificate(s) for node(s) %s", strings.Join(cmd.Nodes, ", "))
	}

	results, err := writeClientCerts(cmd.Output, poolUUID, certs)
	if err != nil {
		return err
	}
	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(results, nil)
	}
	cmd.reportClientCerts(results, len(cmd.Nodes), len(cmd.Tenants))
	return nil
}

type clientCertResult struct {
	CN       string `json:"cn"`
	CertPath string `json:"cert_path"`
	KeyPath  string `json:"key_path"`
	NotAfter string `json:"not_after"`
}

// writeClientCerts stages each cert/key pair under <dir>/<name>/.
func writeClientCerts(dir string, poolUUID uuid.UUID, certs []*control.ClientCert) ([]clientCertResult, error) {
	var results []clientCertResult
	for _, c := range certs {
		outDir := filepath.Join(dir, c.Name)
		if err := os.MkdirAll(outDir, 0700); err != nil {
			return nil, errors.Wrapf(err, "creating directory for %s", c.CN)
		}
		r := clientCertResult{CN: c.CN, NotAfter: c.NotAfter.Format("2006-01-02")}
		r.CertPath, r.KeyPath = security.NodeCertPaths(outDir, poolUUID)
		if err := writeFileOverwrite(r.CertPath, c.CertPEM, 0644); err != nil {
			return nil, errors.Wrapf(err, "writing cert for %s", c.CN)
		}
		if err := writeFileOverwrite(r.KeyPath, c.KeyPEM, 0400); err != nil {
			return nil, errors.Wrapf(err, "writing key for %s", c.CN)
		}
		results = append(results, r)
	}
	return results, nil
}

func (cmd *baseCmd) reportClientCerts(results []clientCertResult, nodes, tenants int) {
	for _, r := range results {
		cmd.Infof("  %s: %s, %s (valid until %s)", r.CN, r.CertPath, r.KeyPath, r.NotAfter)
	}
	kind, count := "node", nodes
	if tenants > 0 {
		kind, count = "tenant", tenants
	}
	cmd.Infof("Certificates issued for %d %s(s)", count, kind)
	cmd.Infof("Deploy each pair to its node in %s/ (readable by the daos_agent user; key mode 0400)",
		security.DefaultNodeCertDir)
}

// poolNodeAuthGenerateCertCmd mints client certificates where the pool CA
// key is kept, without contacting the system.
type poolNodeAuthGenerateCertCmd struct {
	baseCmd
	cfgCmd
	cmdutil.JSONOutputCmd

	Args struct {
		Pool PoolID `positional-arg-name:"<pool UUID>" required:"1"`
	} `positional-args:"yes"`
	CAKey    string       `long:"pool-ca-key" description:"Path to pool CA private key (default: <pool_uuid>_ca.key under pools/ in the dmg certificate directory)"`
	Nodes    []string     `long:"node" description:"Node name(s) to generate certs for"`
	Tenants  []string     `long:"tenant" description:"Tenant name(s) to generate certs for"`
	Output   string       `long:"output" short:"o" description:"Directory for the certificates, one subdirectory per name (default: <pool_uuid>/ under pools/ in the dmg certificate directory)"`
	Validity validityFlag `long:"validity" description:"Certificate lifetime, e.g. 90d, 26w, 2y (default: 1y; never past the pool CA's expiry)"`
}

func (cmd *poolNodeAuthGenerateCertCmd) Execute(args []string) error {
	if (len(cmd.Nodes) == 0) == (len(cmd.Tenants) == 0) {
		return errors.New("specify --node or --tenant, not both")
	}
	if !cmd.Args.Pool.HasUUID() {
		return errors.New("generate-cert takes the pool UUID; a label cannot be resolved without the system")
	}
	poolUUID := cmd.Args.Pool.UUID
	if cmd.CAKey == "" || cmd.Output == "" {
		dir, err := poolCADir(cmd.config)
		if err != nil {
			return err
		}
		if cmd.CAKey == "" {
			_, cmd.CAKey = security.PoolCAPaths(dir, poolUUID)
		}
		if cmd.Output == "" {
			cmd.Output = filepath.Join(dir, poolUUID.String())
		}
	}
	// Revocation state lives on the system; never overwrite a staged cert here.
	for _, name := range append(append([]string{}, cmd.Nodes...), cmd.Tenants...) {
		if certPath, _ := security.NodeCertPaths(filepath.Join(cmd.Output, name), poolUUID); fileExists(certPath) {
			return errors.Errorf("certificate already staged for %s at %s (pass --output to use another directory)", name, certPath)
		}
	}
	if err := ensureWritableDir(cmd.Output); err != nil {
		return errors.Wrapf(err, "certificate directory %s is not writable; fix its permissions or pass --output", cmd.Output)
	}

	certs, err := control.PoolGenerateClientCerts(&control.PoolGenerateClientCertsReq{
		CAKeyPath: cmd.CAKey,
		Nodes:     cmd.Nodes,
		Tenants:   cmd.Tenants,
		Validity:  cmd.Validity.Duration,
	})
	if err != nil {
		return err
	}
	results, err := writeClientCerts(cmd.Output, poolUUID, certs)
	if err != nil {
		return err
	}
	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(results, nil)
	}
	cmd.reportClientCerts(results, len(cmd.Nodes), len(cmd.Tenants))
	cmd.Infof("Revocation state was not consulted: revoke before minting a replacement for a revoked identity")
	return nil
}

// poolNodeAuthRevokeCmd advances the pool's revocation watermark for a CN
// and evicts its active handles.
type poolNodeAuthRevokeCmd struct {
	poolCmd
	Node            string `long:"node" description:"Node name to revoke"`
	Tenant          string `long:"tenant" description:"Tenant name to revoke"`
	EvictAllHandles bool   `long:"evict-all-handles" description:"Evict every active pool handle (default for tenant: revocations)"`
	NoEvict         bool   `long:"no-evict" description:"Advance the watermark but leave active handles alive"`
}

func (cmd *poolNodeAuthRevokeCmd) Execute(args []string) error {
	if cmd.Node == "" && cmd.Tenant == "" {
		return errors.New("specify --node or --tenant")
	}
	if cmd.Node != "" && cmd.Tenant != "" {
		return errors.New("--node and --tenant are mutually exclusive")
	}
	if cmd.EvictAllHandles && cmd.NoEvict {
		return errors.New("--evict-all-handles and --no-evict are mutually exclusive")
	}

	evictMode := control.PoolRevokeEvictDefault
	switch {
	case cmd.EvictAllHandles:
		evictMode = control.PoolRevokeEvictPoolWide
	case cmd.NoEvict:
		evictMode = control.PoolRevokeEvictNone
	}

	resp, err := control.PoolRevokeClient(cmd.MustLogCtx(), cmd.ctlInvoker,
		&control.PoolRevokeClientReq{
			ID:        cmd.PoolID().String(),
			Node:      cmd.Node,
			Tenant:    cmd.Tenant,
			EvictMode: evictMode,
		})
	if err != nil {
		return errors.Wrap(err, "revoking client")
	}

	type revokeResult struct {
		CN             string `json:"cn"`
		Watermark      string `json:"watermark"`
		HandlesEvicted int32  `json:"handles_evicted"`
		EvictScope     string `json:"evict_scope"`
	}
	result := revokeResult{
		CN:             resp.CN,
		Watermark:      resp.Watermark.Format(time.RFC3339),
		HandlesEvicted: resp.HandlesEvicted,
		EvictScope:     resp.EvictScope,
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(result, nil)
	}

	cmd.Infof("Revoked %s", resp.CN)
	cmd.Infof("  Watermark:       %s", result.Watermark)
	cmd.Infof("  Handles evicted: %d (%s)", result.HandlesEvicted, result.EvictScope)
	if cmd.Node != "" {
		cmd.Infof("  Revocation is by certificate identity: a host connecting with a tenant certificate reconnects; revoke the tenant to cut it off")
	}
	cmd.Infof("To restore access, issue a new certificate: dmg pool node-auth issue")
	if result.EvictScope == "machine" && result.HandlesEvicted == 0 {
		cmd.Noticef("No active handles matched %s — verify the CN is correct (the client may legitimately not have an active connection).",
			resp.CN)
	}
	return nil
}
