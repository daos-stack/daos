//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
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
