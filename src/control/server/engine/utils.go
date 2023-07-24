//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package engine

import (
	"fmt"
	"regexp"
	"strings"
	"unicode/utf8"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
)

const (
	// NvmeMinBytesPerTarget is min NVMe pool allocation per target
	NvmeMinBytesPerTarget = 1 * humanize.GiByte
	// ScmMinBytesPerTarget is min SCM pool allocation per target
	ScmMinBytesPerTarget = 16 * humanize.MiByte
)

type (
	cmdLogger struct {
		logFn  func(string)
		prefix string
	}
)

func (cl *cmdLogger) Write(data []byte) (int, error) {
	if cl.logFn == nil {
		return 0, errors.New("no log function set in cmdLogger")
	}

	var msg string
	if cl.prefix != "" {
		msg = cl.prefix + " "
	}
	msg += string(data)
	cl.logFn(msg)
	return len(data), nil
}

const (
	logMasksStrMaxLen    = 1023
	logMasksStrAssignSep = ","
	logMasksStrAssignOp  = "="
)

// LogLevel enum representing engine logging levels.
type LogLevel uint

// LogLevels matching D_LOG API priority strings.
const (
	LogLevelUndefined LogLevel = iota
	LogLevelDbug
	LogLevelInfo
	LogLevelNote
	LogLevelWarn
	LogLevelErr
	LogLevelCrit
	LogLevelAlrt
	LogLevelEmrg
	LogLevelEmit
)

func (ll LogLevel) String() string {
	switch ll {
	case LogLevelDbug:
		return "DBUG"
	case LogLevelInfo:
		return "INFO"
	case LogLevelNote:
		return "NOTE"
	case LogLevelWarn:
		return "WARN"
	case LogLevelErr:
		return "ERR"
	case LogLevelCrit:
		return "CRIT"
	case LogLevelAlrt:
		return "ALRT"
	case LogLevelEmrg:
		return "EMRG"
	case LogLevelEmit:
		return "EMIT"
	default:
		return ""
	}
}

// StrToLogLevel takes an input string and returns a LogLevel type object.
func StrToLogLevel(s string) LogLevel {
	switch strings.ToUpper(s) {
	case "DEBUG", "DBUG":
		return LogLevelDbug
	case "INFO":
		return LogLevelInfo
	case "NOTE":
		return LogLevelNote
	case "WARN":
		return LogLevelWarn
	case "ERROR", "ERR":
		return LogLevelErr
	case "CRIT":
		return LogLevelCrit
	case "ALRT":
		return LogLevelAlrt
	case "FATAL", "EMRG":
		return LogLevelEmrg
	case "EMIT":
		return LogLevelEmit
	default:
		return LogLevelUndefined
	}
}

var (
	validLogLevels = []string{
		"DEBUG", "DBUG", "INFO", "NOTE", "WARN", "ERROR", "ERR", "CRIT", "ALRT", "FATAL",
		"EMRG", "EMIT",
	}
	validLogStreams = []string{
		"ALL",                                                     // Select all streams
		"MD", "PL", "MGMT", "EPC", "DF", "REBUILD", "SEC", "CSUM", // DAOS debug streams
		"GROUP_DEFAULT", "GROUP_METADATA_ONLY", "GROUP_METADATA",
		"ANY", "TRACE", "MEM", "NET", "IO", "TEST", // GURT debug streams
	}
	validLogSubsystems = []string{
		"ALL",                                                    // Select all subsystems
		"DAOS", "ARRAY", "KV", "COMMON", "TREE", "VOS", "CLIENT", // DAOS subsystems
		"SERVER", "RDB", "RSVC", "POOL", "CONTAINER", "OBJECT",
		"PLACEMENT", "REBUILD", "MGMT", "BIO", "TESTS", "DFS", "DUNS",
		"DRPC", "SECURITY", "DTX", "DFUSE", "IL", "CSUM", "STACK",
		"MISC", "MEM", "SWIM", "FI", "TELEM", // Common subsystems (GURT)
		"CRT", "RPC", "BULK", "CORPC", "GRP", "LM", "HG", // CaRT subsystems
		"EXTERNAL", "ST", "IV", "CTL",
	}
	errLogNameAllWithOther = errors.New("'all' identifier cannot be used with any other")
	errLogNameAllInMasks   = errors.New("'all' identifier cannot be used in log mask level assignments")
)

func isLogLevelValid(name string) bool {
	return StrToLogLevel(name) != LogLevelUndefined
}

func errUnknownLogLevel(level string) error {
	return errors.Errorf("unknown log level %q want one of %v", level, validLogLevels)
}

func checkStrChars(in string) error {
	if !utf8.ValidString(in) {
		return errors.New("input is not valid UTF-8")
	}
	if len(in) > logMasksStrMaxLen {
		return errors.Errorf("string exceeds maximum length (%d>%d)",
			len(in), logMasksStrMaxLen)
	}

	re := regexp.MustCompile(`^([a-zA-Z,=_]+)$`)
	matches := re.FindStringSubmatch(in)
	if matches == nil {
		return errors.Errorf("string has illegal characters: %q", in)
	}

	return nil
}

// ValidateLogMasks provides validation for log-masks string specifier.
//
// The input string should look like: PREFIX1=LEVEL1,PREFIX2=LEVEL2,...
// LEVELs are validated here whilst PREFIX (subsystem) is validated server side.
func ValidateLogMasks(masks string) error {
	if masks == "" {
		return nil
	}
	if err := checkStrChars(masks); err != nil {
		return errors.Wrap(err, "log masks")
	}

	for idx, tok := range strings.Split(masks, logMasksStrAssignSep) {
		if idx == 0 && !strings.Contains(tok, logMasksStrAssignOp) {
			if !isLogLevelValid(tok) {
				return errUnknownLogLevel(tok)
			}
			continue // first specifier can exclude subsystem "PREFIX="
		}

		subsysLevel := strings.Split(tok, logMasksStrAssignOp)
		if len(subsysLevel) != 2 {
			return errors.Errorf("tokens after the first separator should be of the "+
				"form PREFIX=LEVEL but token %d is %q", idx, tok)
		}

		subsys := subsysLevel[0]
		level := subsysLevel[1]
		if !isLogLevelValid(level) {
			return errUnknownLogLevel(level)
		}
		if strings.ToUpper(subsys) == "ALL" {
			return errLogNameAllInMasks
		}
		if err := ValidateLogSubsystems(subsys); err != nil {
			return err
		}
	}

	return nil
}

func validateLogNames(input string, validNames []string) error {
	if input == "" {
		return nil
	}
	if err := checkStrChars(input); err != nil {
		return errors.Wrap(err, "check string")
	}

	tokens := strings.Split(input, logMasksStrAssignSep)
	for _, tok := range tokens {
		if !common.Includes(validNames, strings.ToUpper(tok)) {
			return errors.Errorf("unknown name %q want one of %v", tok, validNames)
		}
		if strings.ToUpper(tok) == "ALL" && len(tokens) != 1 {
			return errLogNameAllWithOther
		}
	}

	return nil
}

// ValidateLogStreams provides validation for the stream names provided in the log-masks debug
// streams string. The input string should look like: STREAM1,STREAM2,...
func ValidateLogStreams(streams string) error {
	return errors.Wrap(validateLogNames(streams, validLogStreams),
		"logging debug streams")
}

// ValidateLogSubsystems provides validation for the subsystem names provided in the string. The
// input string should look like: SUBSYS1,SUBSYS2,...
func ValidateLogSubsystems(subsystems string) error {
	return errors.Wrap(validateLogNames(subsystems, validLogSubsystems),
		"validate logging subsystems")
}

func getBaseLogLevel(masks string) LogLevel {
	level := StrToLogLevel(strings.Split(masks, logMasksStrAssignSep)[0])
	if level == LogLevelUndefined {
		return LogLevelErr
	}
	return level
}

type logSubsysLevel struct {
	subsys string
	level  LogLevel
}

func getLogLevelAssignments(masks, subsystemsStr string, baseLevel LogLevel) ([]logSubsysLevel, error) {
	// Enumerate subsystem level assignments from D_LOG_MASK.
	var assignments []logSubsysLevel
	for _, tok := range strings.Split(masks, logMasksStrAssignSep) {
		if !strings.Contains(tok, logMasksStrAssignOp) {
			continue // not a level assignment
		}

		subsysLevel := strings.Split(tok, logMasksStrAssignOp)
		if len(subsysLevel) != 2 {
			return nil, errors.Errorf("illegal log level assignment: want PREFIX=LEVEL got %q",
				subsysLevel)
		}

		assignments = append(assignments, logSubsysLevel{
			subsys: subsysLevel[0],
			level:  StrToLogLevel(subsysLevel[1]),
		})
	}

	// Build subsystems slice from input string.
	var subsystems []string
	for _, ss := range strings.Split(subsystemsStr, logMasksStrAssignSep) {
		if ss == "" || strings.ToUpper(ss) == "ALL" {
			continue
		}
		subsystems = append(subsystems, ss)
	}

	if len(subsystems) == 0 {
		return assignments, nil
	}

	// Remove assignments if assignment level < ERROR and subsystem not in subsystems slice.
	var newAssignments []logSubsysLevel
	for _, a := range assignments {
		if a.level >= LogLevelErr || common.Includes(subsystems, a.subsys) {
			newAssignments = append(newAssignments, a)
		}
	}
	assignments = newAssignments

	if baseLevel >= LogLevelErr {
		return assignments, nil
	}

	// Add assignments if base level < ERROR and subsystem in subsystems slice.
	for _, ss := range subsystems {
		skip := false
		for _, a := range assignments {
			if a.subsys == ss {
				skip = true // assignment already specified for subsystem
				break
			}
		}
		if skip {
			continue
		}
		assignments = append(assignments, logSubsysLevel{
			subsys: ss,
			level:  baseLevel,
		})
	}

	return assignments, nil
}

func genLogMasks(assignments []logSubsysLevel, baseLevel LogLevel) string {
	masks := baseLevel.String()
	for _, a := range assignments {
		masks += fmt.Sprintf(",%s=%s", a.subsys, a.level.String())
	}

	return masks
}

// MergeLogEnvVars merges the value of DD_SUBSYS into D_LOG_MASK. The function takes original
// D_LOG_MASK and DD_SUBSYS values and returns resultant log_mask. The merge is performed by
// evaluating DD_SUBSYS list of subsystems to be enabled and appending subsystem log level
// assignments to D_LOG_MASK list when appropriate.
//
// 1. Return D_LOG_MASK if DD_SUBSYS or D_LOG_MASK are unset
// 2. Identify original base log level from D_LOG_MASK
// 3. Enumerate subsystem level assignments from D_LOG_MASK.
// 4. Remove assignments if level < ERROR and subsystem not in subsystems slice. Log a warning.
// 5. Add assignments if base level < ERROR and subsystem in subsystems slice.
// 6. Remove any assignment where level is equal to the new base level.
// 7. Return new log masks generated from assignments and the new base level.
func MergeLogEnvVars(logMasks, subsystemsStr string) (string, error) {
	if logMasks == "" {
		return "", nil
	}

	// Identify original base log level from D_LOG_MASK string.
	baseLevel := getBaseLogLevel(logMasks)

	// Evaluate log level assignments for individual subsystems.
	assignments, err := getLogLevelAssignments(logMasks, subsystemsStr, baseLevel)
	if err != nil {
		return "", err
	}

	if subsystemsStr == "" || strings.ToUpper(subsystemsStr) == "ALL" {
		return genLogMasks(assignments, baseLevel), nil
	}

	if baseLevel < LogLevelErr {
		baseLevel = LogLevelErr
	}

	// Remove any assignment where level is equal to the new base level.
	for i, a := range assignments {
		if a.level == baseLevel {
			assignments = append(assignments[:i], assignments[i+1:]...)
			i-- // slice just got shorter
		}
	}

	// Generate log masks string from assignments and base (default) log level.
	return genLogMasks(assignments, baseLevel), nil
}
