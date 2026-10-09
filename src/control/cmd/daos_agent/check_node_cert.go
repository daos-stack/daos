//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/security/auth"
)

// checkNodeCertCmd is an admin command used to query the MS for the given pool's
// certificate requirements, and then check the local node's certificate deployment
// against those requirements.
type checkNodeCertCmd struct {
	configCmd
	ctlInvokerCmd
	cmdutil.LogCmd
	cmdutil.JSONOutputCmd
	Args struct {
		Pool string `positional-arg-name:"<pool label or UUID>" required:"1"`
	} `positional-args:"yes"`
}

type nodeCertCheck struct {
	Name  string `json:"name"`
	Value string `json:"value"`
	OK    bool   `json:"ok"`
	Warn  bool   `json:"warn,omitempty"`
}

type nodeCertReport struct {
	checks      []nodeCertCheck
	failed      bool
	notDeployed bool
}

func (r *nodeCertReport) add(name, value string, ok bool) {
	r.checks = append(r.checks, nodeCertCheck{Name: name, Value: value, OK: ok})
	if !ok {
		r.failed = true
	}
}

func (r *nodeCertReport) pass(name, value string) { r.add(name, value, true) }
func (r *nodeCertReport) fail(name, value string) { r.add(name, value, false) }
func (r *nodeCertReport) warn(name, value string) {
	r.checks = append(r.checks, nodeCertCheck{Name: name, Value: value, OK: true, Warn: true})
}

// absent records a missing cert dir or cert file: a hard failure when
// the pool requires certificates, a clean nothing-to-do otherwise.
func (r *nodeCertReport) absent(requires bool, name, value string) {
	if requires {
		r.fail(name, value+" — pool requires node certificates")
		return
	}
	r.pass(name, value)
	r.notDeployed = true
}

func (cmd *checkNodeCertCmd) quietByDefault() {}

// runChecks reports every link between a deployed certificate and a
// successful connect, in the order the agent and server evaluate them.
func (cmd *checkNodeCertCmd) runChecks(state *control.PoolNodeAuthState, msErr error) *nodeCertReport {
	r := &nodeCertReport{}

	if msErr != nil {
		r.fail("management service", fmt.Sprintf(
			"unreachable (%s) — the agent cannot serve this pool at all; "+
				"fix connectivity before anything cert-related", msErr))
		return r
	}
	r.pass("management service", "reachable")
	requires := state.Enabled()
	req := "does not require node certificates"
	if requires {
		req = "requires node certificates"
	}
	r.pass("pool", fmt.Sprintf("%s (%s)", state.PoolUUID, req))

	now := time.Now()
	machine, machineErr := auth.GetMachineName()
	insp := security.InspectNodeCert(cmd.cfg.CredentialConfig.NodeCertDir, state.PoolUUID,
		machine, now, cmd.cfg.TransportConfig.CertMaxClockSkew)

	if !r.reportFiles(insp, requires) {
		return r
	}
	if !r.reportPolicy(insp, machine, machineErr, now) {
		return r
	}
	cmd.reportChain(r, state, insp, now)
	r.reportRevocation(state, insp)
	return r
}

func fileDesc(f security.NodeCertFile) string {
	return fmt.Sprintf("mode %#o, owner %s", f.Mode, f.Owner)
}

// reportFiles covers the directory, the certificate and the key. It
// returns false when there is nothing further to check.
func (r *nodeCertReport) reportFiles(insp *security.NodeCertInspection, requires bool) bool {
	switch {
	case errors.Is(insp.Dir.Err, os.ErrNotExist):
		r.absent(requires, "cert dir", fmt.Sprintf("%s (does not exist)", insp.Dir.Path))
		return false
	case insp.Dir.Err != nil:
		r.fail("cert dir", fmt.Sprintf("%s (%s)", insp.Dir.Path, insp.Dir.Err))
		return false
	}
	r.pass("cert dir", fmt.Sprintf("%s (exists)", insp.Dir.Path))

	switch {
	case errors.Is(insp.Cert.Err, os.ErrNotExist):
		r.absent(requires, "cert file", fmt.Sprintf("%s (not deployed)", filepath.Base(insp.Cert.Path)))
		return false
	case insp.Cert.Err != nil:
		r.fail("cert file", fmt.Sprintf("%s (%s)", insp.Cert.Path, insp.Cert.Err))
		return false
	}
	r.pass("cert file", fmt.Sprintf("%s  found, parses OK", filepath.Base(insp.Cert.Path)))

	if insp.Key.Err != nil {
		desc := insp.Key.Err.Error()
		if insp.Key.Mode != 0 {
			desc = fileDesc(insp.Key) + ", " + desc
		}
		r.fail("key file", fmt.Sprintf("%s (%s)", filepath.Base(insp.Key.Path), desc))
		return false
	}
	r.pass("key file", fmt.Sprintf("%s  found, %s, matches cert", filepath.Base(insp.Key.Path), fileDesc(insp.Key)))
	return true
}

// reportPolicy covers the CN, its binding to this machine, and the
// validity window. It returns false when the CN itself is unusable.
func (r *nodeCertReport) reportPolicy(insp *security.NodeCertInspection, machine string, machineErr error, now time.Time) bool {
	if insp.CNErr != nil {
		r.fail("CN", fmt.Sprintf("%q (%s)", insp.CN, insp.CNErr))
		return false
	}
	r.pass("CN", insp.CN)

	switch {
	case insp.Prefix != security.PoolCertCNPrefixNode:
		r.pass("machine name", "n/a (tenant certificate)")
	case machineErr != nil:
		r.fail("machine name", fmt.Sprintf("cannot determine local machine name: %s", machineErr))
	case insp.Binding != nil:
		r.fail("machine name", fmt.Sprintf("%s  (MISMATCH: cert is for %q)", machine, insp.Suffix))
	default:
		r.pass("machine name", fmt.Sprintf("%s  (match)", machine))
	}

	cert := insp.Certificate
	window := fmt.Sprintf("%s .. %s",
		cert.NotBefore.Format("2006-01-02"), cert.NotAfter.Format("2006-01-02"))
	switch {
	case errors.Is(insp.Validity, security.ErrCertNotYetValid):
		r.warn("validity", fmt.Sprintf("%s (%s; rejected by the server unless its transport_config.cert_max_clock_skew covers the gap)", window, insp.Validity))
	case insp.Validity != nil:
		r.fail("validity", fmt.Sprintf("%s (%s)", window, insp.Validity))
	default:
		if w := cmdutil.CertExpiryWarning("certificate", cert.NotAfter, now); w != "" {
			r.warn("validity", fmt.Sprintf("%s (%s — reissue)", window, w))
		} else {
			r.pass("validity", fmt.Sprintf("%s (OK)", window))
		}
	}
	return true
}

// reportChain verifies the deployed cert against the pool's current CA bundle.
func (cmd *checkNodeCertCmd) reportChain(r *nodeCertReport, state *control.PoolNodeAuthState, insp *security.NodeCertInspection, now time.Time) {
	if !state.Enabled() {
		r.pass("chain", "not checked (pool has no CA)")
		return
	}

	root, err := cmd.cfg.TransportConfig.CACert()
	if err != nil {
		r.fail("chain", fmt.Sprintf("cannot load DAOS CA (transport_config.ca_cert): %s", err))
		return
	}
	if root == nil {
		r.fail("chain", "cannot verify: transport security is disabled (allow_insecure)")
		return
	}

	if err := security.VerifyNodeCertChain(insp.Certificate, root, state.CABundle, now.Add(cmd.cfg.TransportConfig.CertMaxClockSkew)); err != nil {
		r.fail("chain", fmt.Sprintf("does NOT chain to the pool's current CA (%s) — "+
			"reissue from the current CA", err))
		return
	}
	r.pass("chain", "verifies against the pool's current CA")
	for _, e := range state.CAs {
		what := fmt.Sprintf("pool CA %q", e.Cert.Subject.CommonName)
		if w := cmdutil.CertExpiryWarning(what, e.Cert.NotAfter, now); w != "" {
			r.warn("pool CA", w+" — rotate the pool CA (dmg pool node-auth add-ca)")
		}
	}
}

func (r *nodeCertReport) reportRevocation(state *control.PoolNodeAuthState, insp *security.NodeCertInspection) {
	if !state.Enabled() {
		return
	}
	if err := security.CheckCertRevocation(insp.Certificate, insp.CN, state.Watermarks); err != nil {
		r.fail("revocation", fmt.Sprintf("REVOKED (%s) — reissue", err))
		return
	}
	r.pass("revocation", "not revoked")
}

func (cmd *checkNodeCertCmd) Execute(_ []string) error {
	state, err := control.GetPoolNodeAuthState(cmd.MustLogCtx(), cmd.ctlInvoker, cmd.Args.Pool)
	r := cmd.runChecks(state, err)

	if cmd.JSONOutputEnabled() {
		result := struct {
			Deployed bool            `json:"deployed"`
			Requires bool            `json:"requires_certs"`
			Checks   []nodeCertCheck `json:"checks"`
			Passed   bool            `json:"passed"`
		}{
			Deployed: !r.notDeployed,
			Requires: state != nil && state.Enabled(),
			Checks:   r.checks,
			Passed:   !r.failed,
		}
		return cmd.OutputJSON(result, nil)
	}

	// The report goes to stdout regardless of the agent's log configuration.
	for _, chk := range r.checks {
		marker := ""
		if !chk.OK {
			marker = "FAIL "
		} else if chk.Warn {
			marker = "WARN "
		}
		fmt.Printf("  %-20s %s%s\n", chk.Name+":", marker, chk.Value)
	}
	if r.notDeployed {
		fmt.Println("Pool does not require node certificates; nothing to do.")
		return nil
	}
	if r.failed {
		return errors.New("node certificate check failed")
	}
	return nil
}
