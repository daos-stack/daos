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
	"fmt"
	"io"
	"time"

	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"

	"golang.org/x/net/context"
)

// FeatureMap is an alias for mgmt features supported by gRPC server.
type FeatureMap map[string]string

// FeatureResult contains results and error of a request
type FeatureResult struct {
	Fm  FeatureMap
	Err error
}

// ClientFeatureMap is an alias for management features supported on server
// connected to given client.
type ClientFeatureMap map[string]FeatureResult

// getFeature returns a feature from a requested name.
func (c *control) getFeature(name string) (*pb.Feature, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	return c.client.GetFeature(ctx, &pb.FeatureName{Name: name})
}

// listAllFeatures returns map of all supported management features.
func (c *control) listAllFeatures() (fm FeatureMap, err error) {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	stream, err := c.client.ListAllFeatures(ctx, &pb.EmptyParams{})
	if err != nil {
		return
	}

	fm = make(FeatureMap)
	var f *pb.Feature
	for {
		f, err = stream.Recv()
		if err == io.EOF {
			err = nil
			break
		} else if err != nil {
			return
		}
		fm[f.Fname.Name] = fmt.Sprintf(
			"category %s, %s", f.Category.Category, f.Description)
	}

	return
}

// listFeatures returns supported management features for a given category.
func (c *control) listFeatures(category string) (
	fm FeatureMap, err error) {

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	stream, err := c.client.ListFeatures(ctx, &pb.Category{Category: category})
	if err != nil {
		return
	}

	fm = make(FeatureMap)
	var f *pb.Feature
	for {
		f, err = stream.Recv()
		if err == io.EOF {
			err = nil
			break
		} else if err != nil {
			return
		}
		fm[f.Fname.Name] = f.Description
	}
	return
}

// listFeaturesRequest is to be called as a goroutine and returns result
// containing supported server features over channel.
func listFeaturesRequest(controller Control, ch chan ClientResult) {
	fMap, err := controller.listAllFeatures()
	ch <- ClientResult{controller.getAddress(), fMap, err}
}

// ListFeatures returns supported management features for each server connected.
func (c *connList) ListFeatures() ClientFeatureMap {
	var err error
	cResults := c.makeRequests(listFeaturesRequest)
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
