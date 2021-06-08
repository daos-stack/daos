//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"math/rand"
	"strings"
	"time"
)

const (
	// Use ISO8601 format for timestamps as it's
	// widely supported by parsers (e.g. javascript, etc).
	//
	// NB: ISO8601 is very similar to RFC3339 format; the
	// primary difference is that the UTC zone is represented
	// as -00:00 instead of Z.
	iso8601 = "2006-01-02T15:04:05.000-07:00"

	defaultJitter = 500 * time.Millisecond
)

// FormatTime returns ISO8601 formatted representation of timestamp with
// microsecond resolution.
func FormatTime(t time.Time) string {
	return t.Format(iso8601)
}

// ParseTime returns a time.Time object from ISO8601 or RFC3339
// timestamp strings.
func ParseTime(ts string) (time.Time, error) {
	if t, err := time.Parse(time.RFC3339Nano, ts); err == nil {
		return t, nil
	}

	// The strftime() offset doesn't include a colon. If the normal
	// format string doesn't work, try this one.
	idx := strings.LastIndex(time.RFC3339Nano, ":")
	fmtRunes := []rune(time.RFC3339Nano)
	fmtStr := string(append(fmtRunes[0:idx], fmtRunes[idx+1:]...))
	return time.Parse(fmtStr, ts)
}

// ExpBackoffWithJitter is like ExpBackoff but allows for a custom
// amount of jitter to be specified.
func ExpBackoffWithJitter(base, jitter time.Duration, cur, limit uint64) time.Duration {
	// Special case: If the current iteration is 0, just return
	// a zero duration.
	if cur == 0 {
		return 0
	}

	min := func(a, b uint64) uint64 {
		if a < b {
			return a
		}
		return b
	}

	backoff := base
	pow := min(cur, limit)
	for pow > 2 {
		backoff *= 2
		pow--
	}

	backoff += jitter * time.Duration(rand.Int63n(int64(limit)))

	return backoff

}

// ExpBackoff implements an exponential backoff algorithm, where the
// parameters are used to multiplicatively slow down the rate of retries
// of some operation, up to a maximum backoff limit.
func ExpBackoff(base time.Duration, cur, limit uint64) time.Duration {
	return ExpBackoffWithJitter(base, defaultJitter, cur, limit)
}
