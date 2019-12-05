//
// (C) Copyright 2018-2019 Intel Corporation.
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

package spdk

import (
	"fmt"
	"testing"
)

func checkFailure(shouldSucceed bool, err error) (rErr error) {
	switch {
	case err != nil && shouldSucceed:
		rErr = fmt.Errorf("expected test to succeed, failed unexpectedly: %v", err)
	case err == nil && !shouldSucceed:
		rErr = fmt.Errorf("expected test to fail, succeeded unexpectedly")
	}

	return
}

func TestDiscover(t *testing.T) {
	//	var se Env
	//	var n Nvme

	tests := []struct {
		shmID         int
		shouldSucceed bool
	}{
		{
			shmID:         0,
			shouldSucceed: true,
		},
		//		{
		//			shmID:         1,
		//			shouldSucceed: true,
		//		},
	}

	for _, _ = range tests {
		fmt.Println("spdk binding tests currently disabled")

		// TODO
		//		if err := se.InitSPDKEnv(tt.shmID); err != nil {
		//			t.Fatal(err.Error())
		//		}
		//
		//		cs, nss, err := n.Discover()
		//		if checkFailure(tt.shouldSucceed, err) != nil {
		//			t.Errorf("case %d: %v", i, err)
		//		}
		//		fmt.Printf("controllers: %#v\n", cs)
		//		fmt.Printf("namespaces: %#v\n", nss)

		//		_, _, err = n.Update(0, "", 0)
		//		if checkFailure(tt.shouldSucceed, err) != nil {
		//			t.Errorf("case %d: %v", i, err)
		//		}

		//		n.Cleanup()
	}
}
