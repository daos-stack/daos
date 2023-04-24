//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package logging

import (
	"fmt"
	"io"
	"log"
	"os"
)

const noticeLogFlags = log.LstdFlags

// NewCommandLineNoticeLogger returns a NoticeLogger configured
// for outputting unadorned notice messages (i.e. no
// timestamps, source info, etc); typically used for CLI
// utility logging.
func NewCommandLineNoticeLogger(output io.Writer) *DefaultNoticeLogger {
	return &DefaultNoticeLogger{
		baseLogger{
			dest: output,
			log:  log.New(output, "NOTICE: ", emptyLogFlags),
		},
	}
}

// NewNoticeLogger returns NoticeLogger configured for outputting
// notice messages with standard formatting (e.g. to stderr,
// logfile, etc.)
func NewNoticeLogger(prefix string, output io.Writer) *DefaultNoticeLogger {
	loggerPrefix := "NOTICE "
	if prefix != "" {
		loggerPrefix = prefix + " " + loggerPrefix
	}
	return &DefaultNoticeLogger{
		baseLogger{
			dest:   output,
			prefix: prefix,
			log:    log.New(output, loggerPrefix, noticeLogFlags),
		},
	}
}

// DefaultNoticeLogger implements the NoticeLogger interface.
type DefaultNoticeLogger struct {
	baseLogger
}

// Noticef emits a formatted notice message.
func (l *DefaultNoticeLogger) Noticef(format string, args ...interface{}) {
	out := fmt.Sprintf(format, args...)
	if err := l.log.Output(logOutputDepth, out); err != nil {
		fmt.Fprintf(os.Stderr, "logger Noticef() failed: %s\n", err)
	}
}
