//
// (C) Copyright 2019 Intel Corporation.
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
// +build gofuzz

package hostlist

// Fuzz is used to subject the library to randomized inputs in order to
// identify any deficiencies in input parsing and/or error handling.
// The number of inputs that may result in errors is infinite; the number
// of inputs that result in crashes should be zero.
//
// This function is only built by go-fuzz, using go-fuzz-build. See
// https://github.com/dvyukov/go-fuzz for details on installing and
// running the fuzzer.
//
// The most recent run:
// 2019/11/15 07:24:09 workers: 4, corpus: 641 (1h10m ago), crashers: 0, restarts: 1/9998, execs: 134399056 (3727/sec), cover: 1339, uptime: 10h1m
func Fuzz(data []byte) int {
	hs, err := CreateSet(string(data))
	if err != nil {
		return 0
	}

	_ = hs.String()

	if _, err := hs.Delete(string(data)); err != nil && err != ErrEmpty {
		panic(err)
	}

	return 1
}
