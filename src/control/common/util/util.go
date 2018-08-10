//
// (C) Copyright 2018 Intel Corporation.
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

package util

import (
	"fmt"
	"log"
	"runtime"

	"google.golang.org/grpc/status"
)

// LogGrpcErr is a decorator that adds function name to gRPC error context.
func LogGrpcErr(err error) error {
	errStatus, _ := status.FromError(err)
	function, _, _, _ := runtime.Caller(1)

	// replace with new elaborated error
	err = status.Errorf(
		errStatus.Code(),
		fmt.Sprintf(
			"%v(_): %v",
			runtime.FuncForPC(function).Name(),
			errStatus.Message()))

	log.Println(err)

	return err
}
