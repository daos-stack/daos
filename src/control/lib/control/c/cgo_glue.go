//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// Package-internal cgo glue: panic recovery, the export wrapper, the
// management context, error → rc translation, and C↔Go conversion helpers
// shared by every export.

package main

/*
#include <stdlib.h>
#include <stdint.h>
#include <uuid/uuid.h>
#include <daos_types.h>
#include <daos_prop.h>

#include "daos_control_util.h"
*/
import "C"
import (
	"context"
	"errors"
	"fmt"
	"io"
	"os"
	"os/user"
	"regexp"
	"runtime/cgo"
	"runtime/debug"
	"strconv"
	"strings"
	"unsafe"

	"github.com/google/uuid"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

// init scrubs HTTP_PROXY/etc from the process environment at .so load time.
// gRPC-Go's default dialer honors these vars, but management RPCs are direct
// dials on the control network and must not be routed through any proxy. The
// standalone Go binaries do this from main(); a c-shared library has no
// main(), so it goes in package init before any export can run.
func init() {
	common.ScrubProxyVariables()
}

// recoverExport converts any panic into an error return code. Every cgo-
// exported function returning C.int should defer this with a pointer to its
// named return, so that a Go panic does not propagate across the cgo boundary
// (where it would SIGABRT the process). The panic and a stack trace are
// written to stderr for post-mortem diagnosis.
func recoverExport(rc *C.int) {
	if r := recover(); r != nil {
		fmt.Fprintf(os.Stderr, "panic in libdaos_control export: %v\n%s\n", r, debug.Stack())
		*rc = C.int(daos.MiscError)
	}
}

// recoverExportVoid is recoverExport for exports that do not return a code.
func recoverExportVoid() {
	if r := recover(); r != nil {
		fmt.Fprintf(os.Stderr, "panic in libdaos_control export: %v\n%s\n", r, debug.Stack())
	}
}

// withContext wraps the standard //export dance: panic recovery, handle
// lookup, and Go-error → rc translation. The closure returns nil for
// success or an error for failure (a daos.Status, or anything errorToRC
// understands). getContext failures short-circuit before the closure runs.
func withContext(handle C.uintptr_t, fn func(*ctrlContext) error) (rc C.int) {
	defer recoverExport(&rc)
	ctx, errRC := getContext(handle)
	if errRC != 0 {
		return errRC
	}
	if err := fn(ctx); err != nil {
		rc := errorToRC(err)
		// When errorToRC exhausts every specific check and falls back to
		// DER_MISC, leave a breadcrumb so the next reader of the trace log
		// can see what kind of error escaped classification.
		if rc == int(daos.MiscError) && ctx.log != nil {
			ctx.log.Debugf("errorToRC: no mapping for %T (%s); using DER_MISC",
				err, summarizeRPCErr(err))
		}
		return C.int(rc)
	}
	return 0
}

// ctrlContext holds the client connection state for a management context.
type ctrlContext struct {
	client  control.UnaryInvoker
	log     *logging.LeveledLogger
	logFile *os.File
}

// newContext creates a new management context from a config file path.
// If configFile is empty, uses default config (localhost, insecure mode).
// logFilePath and logLevelStr configure logging; empty strings use defaults.
func newContext(configFile, logFilePath, logLevelStr string) (*ctrlContext, error) {
	var cfg *control.Config
	var err error

	if configFile == "" {
		cfg = control.DefaultConfig()
		cfg.TransportConfig.AllowInsecure = true
	} else {
		cfg, err = control.LoadConfig(configFile)
		if err != nil {
			return nil, err
		}
	}

	var logDest io.Writer = io.Discard
	var logFile *os.File

	if logFilePath != "" {
		logFile, err = os.OpenFile(logFilePath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
		if err != nil {
			return nil, err
		}
		logDest = logFile
	}

	// Notice and Error also go to stderr so that when libdaos_control is
	// loaded inside a test binary (e.g. daos_test) the failure reason is
	// captured by the job manager's stdout/stderr collection even if the
	// log file is unavailable or not collected. Debug/Info remain
	// log-file-only to avoid spamming the test transcript.
	log := logging.NewCombinedLogger("daos_control", logDest).
		WithLogLevel(logging.LogLevelNotice).
		WithNoticeLogger(logging.NewNoticeLogger("daos_control", os.Stderr)).
		WithErrorLogger(logging.NewErrorLogger("daos_control", os.Stderr))

	if logLevelStr != "" {
		var level logging.LogLevel
		if err := level.SetString(logLevelStr); err == nil {
			log.SetLevel(level)
		} else {
			log.Noticef("daos_control: ignoring invalid log_level %q: %s; keeping %s",
				logLevelStr, err, logging.LogLevelNotice)
		}
	}

	client := control.NewClient(
		control.WithConfig(cfg),
		control.WithClientLogger(log),
		control.WithClientComponent(build.ComponentAdmin),
	)

	return &ctrlContext{
		client:  client,
		log:     log,
		logFile: logFile,
	}, nil
}

// ctx returns a background context for operations.
func (c *ctrlContext) ctx() context.Context {
	ctx, _ := logging.ToContext(context.Background(), c.log)
	return ctx
}

// close releases resources associated with the context.
func (c *ctrlContext) close() {
	if c.logFile != nil {
		c.logFile.Close()
	}
}

// newTestContext creates a context with a mock invoker for testing.
func newTestContext(client control.UnaryInvoker, log *logging.LeveledLogger) *ctrlContext {
	if log == nil {
		log = logging.NewCombinedLogger("test", io.Discard)
	}
	return &ctrlContext{
		client: client,
		log:    log,
	}
}

// errInvalidHandle is returned when a zero/invalid handle is provided.
var errInvalidHandle = errors.New("invalid control API handle")

// getContext validates the handle and retrieves the context. Returns nil and
// a non-zero rc if the handle is invalid. cgo.Handle.Value() panics on a
// never-allocated or freed handle; that panic is caught by recoverExport at
// the cgo boundary, not here.
func getContext(handle C.uintptr_t) (*ctrlContext, C.int) {
	if handle == 0 {
		return nil, C.int(errorToRC(errInvalidHandle))
	}

	c, ok := cgo.Handle(handle).Value().(*ctrlContext)
	if !ok {
		return nil, C.int(errorToRC(errInvalidHandle))
	}
	return c, 0
}

// errorToRC converts a Go error to a DAOS return code.
func errorToRC(err error) int {
	if err == nil {
		return 0
	}

	var ds daos.Status
	if errors.As(err, &ds) {
		return int(ds)
	}

	if errors.Is(err, errInvalidHandle) {
		return int(daos.InvalidInput)
	}
	if errors.Is(err, control.ErrNoConfigFile) {
		return int(daos.BadPath)
	}

	if errors.Is(err, os.ErrNotExist) {
		return int(daos.Nonexistent)
	}
	if errors.Is(err, os.ErrPermission) {
		return int(daos.NoPermission)
	}

	if errors.Is(err, context.DeadlineExceeded) {
		return int(daos.TimedOut)
	}
	if errors.Is(err, context.Canceled) {
		return int(daos.Canceled)
	}

	// Many control-plane paths stringify a daos.Status into the error message
	// (see daos.Status.Error()). As a last resort before collapsing to
	// MiscError, recover the embedded status code so callers can distinguish
	// e.g. DER_BUSY from DER_NONEXIST.
	if st, ok := statusFromMsg(err.Error()); ok {
		return int(st)
	}

	// Surface gRPC transport classes so a dial failure (Unavailable) or a
	// server-side classified error doesn't collapse to a generic DER_MISC.
	if gs, ok := status.FromError(err); ok {
		switch gs.Code() {
		case codes.Unavailable:
			return int(daos.Unreachable)
		case codes.DeadlineExceeded:
			return int(daos.TimedOut)
		case codes.Canceled:
			return int(daos.Canceled)
		case codes.NotFound:
			return int(daos.Nonexistent)
		case codes.AlreadyExists:
			return int(daos.Exists)
		case codes.PermissionDenied, codes.Unauthenticated:
			return int(daos.NoPermission)
		case codes.ResourceExhausted:
			return int(daos.NoSpace)
		case codes.InvalidArgument:
			return int(daos.InvalidInput)
		case codes.Unimplemented:
			return int(daos.NotImpl)
		case codes.Aborted:
			return int(daos.TryAgain)
		}
	}

	return int(daos.MiscError)
}

// summarizeRPCErr returns a one-line err string for logging. When the message
// embeds an HTTP response (gRPC transport errors via an HTTP proxy, for
// example, where the response body has been %q-escaped by the inner wrapper),
// the body is compacted to its status line plus Server header so the actual
// failure isn't buried under escaped HTML.
func summarizeRPCErr(err error) string {
	if err == nil {
		return ""
	}
	msg := err.Error()
	hidx := strings.Index(msg, "HTTP/")
	if hidx < 0 {
		return msg
	}
	rest := msg[hidx:]
	// Status line terminator: either the literal 4-char "\r\n" sequence
	// (when an inner wrapper has %q-escaped the response) or an actual CRLF.
	nl := strings.Index(rest, `\r\n`)
	if nl < 0 {
		nl = strings.Index(rest, "\r\n")
	}
	if nl <= 0 {
		return msg
	}
	summary := msg[:hidx] + rest[:nl]
	if k := strings.Index(rest, "Server: "); k > 0 {
		srv := rest[k+len("Server: "):]
		l := strings.Index(srv, `\r\n`)
		if l < 0 {
			l = strings.Index(srv, "\r\n")
		}
		if l > 0 {
			summary += " (Server: " + srv[:l] + ")"
		}
	}
	return summary
}

// derStatusPattern matches a daos.Status.Error() token, which formats as
// "DER_X(-code)". Anchoring to the DER_ identifier avoids false matches on
// unrelated parenthesized numbers in wrapper text (e.g. "retry(-1)").
var derStatusPattern = regexp.MustCompile(`DER_[A-Z0-9_]+\((-?\d+)\)`)

// statusFromMsg extracts a daos.Status from a message produced by
// daos.Status.Error(), which formats as "DER_X(-code): description". Returns
// (0, false) if no DER_XX(-code) token is present.
func statusFromMsg(msg string) (daos.Status, bool) {
	for _, m := range derStatusPattern.FindAllStringSubmatch(msg, -1) {
		code, err := strconv.Atoi(m[1])
		if err == nil && code <= 0 && code > -10000 {
			return daos.Status(code), true
		}
	}
	return 0, false
}

// firstErroredStatus walks items and returns a DAOS status for the first
// one that errored. Returns nil if none errored.
func firstErroredStatus[T any](items []T, getErr func(T) error) error {
	for _, it := range items {
		err := getErr(it)
		if err == nil {
			continue
		}
		var ds daos.Status
		if errors.As(err, &ds) {
			return ds
		}
		if st, ok := statusFromMsg(err.Error()); ok {
			return st
		}
		return daos.MiscError
	}
	return nil
}

// firstRankStatus adapts firstErroredStatus for []*control.PoolRankResult.
func firstRankStatus(results []*control.PoolRankResult) error {
	return firstErroredStatus(results, func(r *control.PoolRankResult) error {
		if r == nil || !r.Errored {
			return nil
		}
		return errors.New(r.Msg)
	})
}

// firstMemberStatus adapts firstErroredStatus for system.MemberResults.
func firstMemberStatus(results system.MemberResults) error {
	return firstErroredStatus(results, func(r *system.MemberResult) error {
		if r == nil || !r.Errored {
			return nil
		}
		return errors.New(r.Msg)
	})
}

// logAllHostErrors emits each host error in the map to the context log at
// Error level, so an rc that collapses them doesn't lose the diagnostic.
func logAllHostErrors(ctx *ctrlContext, hem control.HostErrorsMap) {
	if ctx == nil || ctx.log == nil {
		return
	}
	for _, key := range hem.Keys() {
		hes, ok := hem[key]
		if !ok || hes == nil || hes.HostError == nil {
			continue
		}
		hosts := ""
		if hes.HostSet != nil {
			hosts = hes.HostSet.RangedString()
		}
		ctx.log.Errorf("host(s) %s: %s", hosts, hes.HostError)
	}
}

// firstHostStatus adapts firstErroredStatus for control.HostErrorsMap.
func firstHostStatus(hem control.HostErrorsMap) error {
	keys := hem.Keys()
	return firstErroredStatus(keys, func(k string) error {
		hes, ok := hem[k]
		if !ok || hes == nil || hes.HostError == nil {
			return nil
		}
		return hes.HostError
	})
}

// uuidFromC converts a C uuid_t to a Go uuid.UUID.
func uuidFromC(cUUID *C.uuid_t) uuid.UUID {
	if cUUID == nil {
		return uuid.Nil
	}
	var goUUID uuid.UUID
	for i := 0; i < 16; i++ {
		goUUID[i] = byte(cUUID[i])
	}
	return goUUID
}

// poolIDFromC converts a C uuid_t to a pool ID string.
func poolIDFromC(cUUID *C.uuid_t) string {
	return uuidFromC(cUUID).String()
}

// copyUUIDToC copies a Go uuid.UUID to a C uuid_t.
func copyUUIDToC(goUUID uuid.UUID, cUUID *C.uuid_t) {
	if cUUID == nil {
		return
	}
	for i := 0; i < 16; i++ {
		cUUID[i] = C.uchar(goUUID[i])
	}
}

// rankListFromC converts a C d_rank_list_t to a slice of ranklist.Rank.
func rankListFromC(cRankList *C.d_rank_list_t) []ranklist.Rank {
	if cRankList == nil || cRankList.rl_nr == 0 || cRankList.rl_ranks == nil {
		return nil
	}

	ranks := make([]ranklist.Rank, cRankList.rl_nr)
	cRanks := unsafe.Slice(cRankList.rl_ranks, cRankList.rl_nr)
	for i, r := range cRanks {
		ranks[i] = ranklist.Rank(r)
	}
	return ranks
}

// copyRankListToC copies up to maxLen ranks from a []ranklist.Rank into a
// caller-pre-allocated C d_rank_list_t and sets rl_nr to the number of ranks
// actually written.
func copyRankListToC(ranks []ranklist.Rank, cRankList *C.d_rank_list_t, maxLen int) {
	if cRankList == nil {
		return
	}
	if cRankList.rl_ranks == nil || maxLen <= 0 {
		cRankList.rl_nr = 0
		return
	}

	toCopy := len(ranks)
	if toCopy > maxLen {
		toCopy = maxLen
	}

	cRankList.rl_nr = C.uint32_t(toCopy)
	if toCopy == 0 {
		return
	}
	cRanks := unsafe.Slice(cRankList.rl_ranks, toCopy)
	for i := 0; i < toCopy; i++ {
		cRanks[i] = C.d_rank_t(ranks[i])
	}
}

// goString safely converts a C string to a Go string, returning empty string for nil.
func goString(cStr *C.char) string {
	if cStr == nil {
		return ""
	}
	return C.GoString(cStr)
}

// uidToUsername converts a numeric UID to a username.
func uidToUsername(uid uint32) (string, error) {
	u, err := user.LookupId(strconv.FormatUint(uint64(uid), 10))
	if err != nil {
		return "", fmt.Errorf("uid %d lookup: %s: %w", uid, err, daos.InvalidInput)
	}
	return u.Username, nil
}

// gidToGroupname converts a numeric GID to a group name.
func gidToGroupname(gid uint32) (string, error) {
	g, err := user.LookupGroupId(strconv.FormatUint(uint64(gid), 10))
	if err != nil {
		return "", fmt.Errorf("gid %d lookup: %s: %w", gid, err, daos.InvalidInput)
	}
	return g.Name, nil
}

// propsFromC converts a daos_prop_t to Go PoolProperty values for a
// pool-create request.
func propsFromC(cProps *C.daos_prop_t) ([]*daos.PoolProperty, error) {
	if cProps == nil || cProps.dpp_nr == 0 {
		return nil, nil
	}

	entries := unsafe.Slice(cProps.dpp_entries, cProps.dpp_nr)
	registry := daos.PoolProperties()
	numToName := make(map[uint32]string, len(registry))
	for name, handler := range registry {
		numToName[handler.GetProperty(name).Number] = name
	}

	var props []*daos.PoolProperty
	for i := range entries {
		entry := &entries[i]
		propNum := uint32(entry.dpe_type)

		name, ok := numToName[propNum]
		if !ok {
			return nil, daos.NotSupported
		}
		p := registry[name].GetProperty(name)

		switch propNum {
		case C.DAOS_PROP_PO_LABEL:
			if s := C.get_dpe_str(entry); s != nil {
				p.Value.SetString(C.GoString(s))
			}
		case C.DAOS_PROP_PO_ACL,
			C.DAOS_PROP_PO_OWNER,
			C.DAOS_PROP_PO_OWNER_GROUP,
			C.DAOS_PROP_PO_SVC_LIST:
			return nil, daos.NotSupported
		default:
			p.Value.SetNumber(uint64(C.get_dpe_val(entry)))
		}

		props = append(props, p)
	}

	return props, nil
}
