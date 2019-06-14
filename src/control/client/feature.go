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

package client

import (
	"bytes"
	"fmt"
	"io"
	"sort"
	"time"

	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"

	"golang.org/x/net/context"
)

// FeatureMap is an alias for mgmt features supported by gRPC server.
type FeatureMap map[string]string

func (fm FeatureMap) String() string {
	var buf bytes.Buffer

	for k, v := range fm {
		fmt.Fprintf(&buf, "%s: %s\n", k, v)
	}

	return buf.String()
}

// FeatureResult contains results and error of a request
type FeatureResult struct {
	Fm  FeatureMap
	Err error
}

func (fr FeatureResult) String() string {
	if fr.Err != nil {
		return fr.Err.Error()
	}
	return fr.Fm.String()
}

// ClientFeatureMap is an alias for management features supported on server
// connected to given client.
type ClientFeatureMap map[string]FeatureResult

func (cfm ClientFeatureMap) String() string {
	var buf bytes.Buffer
	servers := make([]string, 0, len(cfm))

	for server := range cfm {
		servers = append(servers, server)
	}
	sort.Strings(servers)

	for _, server := range servers {
		fmt.Fprintf(&buf, "%s:\n%s\n", server, cfm[server])
	}

	return buf.String()
}

// listAllFeatures returns map of all supported management features.
// listFeaturesRequest is to be called as a goroutine and returns result
// containing supported server features over channel.
func listFeaturesRequest(mc Control, i interface{}, ch chan ClientResult) {
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	stream, err := mc.getCtlClient().ListFeatures(ctx, &pb.EmptyReq{})
	if err != nil {
		ch <- ClientResult{mc.getAddress(), nil, err}
		return
	}

	fm := make(FeatureMap)
	var f *pb.Feature
	for {
		f, err = stream.Recv()
		if err == io.EOF {
			err = nil
			break
		} else if err != nil {
			ch <- ClientResult{mc.getAddress(), nil, err}
			return
		}
		fm[f.Fname.Name] = fmt.Sprintf(
			"category %s, %s", f.Category.Category, f.Description)
	}

	ch <- ClientResult{mc.getAddress(), fm, nil}
}

// ListFeatures returns supported management features for each server connected.
func (c *connList) ListFeatures() ClientFeatureMap {
	var err error
	cResults := c.makeRequests(nil, listFeaturesRequest)
	cFeatures := make(ClientFeatureMap) // mapping of server addresses to features

	for _, res := range cResults {
		if res.Err != nil {
			cFeatures[res.Address] = FeatureResult{nil, res.Err}
			continue
		}

		// extract obj from generic ClientResult type returned over channel
		fMap, ok := res.Value.(FeatureMap)
		if !ok {
			err = fmt.Errorf(
				"type assertion failed, wanted %+v got %+v",
				FeatureMap{}, res.Value)
		}

		cFeatures[res.Address] = FeatureResult{fMap, err}
	}

	return cFeatures
}
