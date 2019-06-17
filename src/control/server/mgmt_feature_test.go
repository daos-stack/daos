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

package main

import (
	"testing"

	. "github.com/daos-stack/daos/src/control/common"

	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

// TODO: add server side streaming test for list features

func TestGetFeature(t *testing.T) {
	cs := defaultMockControlService(t)

	mockFeature := MockFeaturePB()
	fMap := make(FeatureMap)
	fMap[mockFeature.Fname.Name] = mockFeature
	cs.supportedFeatures = fMap

	feature, err := cs.GetFeature(nil, mockFeature.Fname)
	if err != nil {
		t.Fatal(err)
	}

	AssertEqual(t, feature, mockFeature, "")

	feature, err = cs.GetFeature(nil, &pb.FeatureName{Name: "non-existent"})
	if err == nil {
		t.Fatal(err)
	}
}
