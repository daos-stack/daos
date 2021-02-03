//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"math/rand"
	"time"
)

const (
	// Use ISO8601 format for timestamps as it's
	// widely supported by parsers (e.g. javascript, etc).
	iso8601NoMicro = "2006-01-02T15:04:05Z0700"
	iso8601        = "2006-01-02T15:04:05.000000Z0700"

	defaultJitter = 500 * time.Millisecond
)

// FormatTime returns ISO8601 formatted representation of timestamp with
// microsecond resolution.
func FormatTime(t time.Time) string {
	return t.Format(iso8601)
}

// FormatTimeNoMicro returns ISO8601 formatted representation of timestamp with
// second resolution.
func FormatTimeNoMicro(t time.Time) string {
	return t.Format(iso8601NoMicro)
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
