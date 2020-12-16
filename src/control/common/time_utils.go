//
// (C) Copyright 2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package common

import (
	"time"
)

const (
	// Use ISO8601 format for timestamps as it's
	// widely supported by parsers (e.g. javascript, etc).
	iso8601NoMicro = "2006-01-02T15:04:05Z0700"
	iso8601        = "2006-01-02T15:04:05.000000Z0700"
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
