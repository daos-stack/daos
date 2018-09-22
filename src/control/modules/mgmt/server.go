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

package mgmt

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"

	"common/log"

	pb "modules/mgmt/proto"
)

var jsonDBRelPath = "share/control/mgmtinit_db.json"

// getAbsInstallPath retrieves absolute path of files in daos install dir
func getAbsInstallPath(relPath string) (string, error) {
	ex, err := os.Executable()
	if err != nil {
		return "", err
	}

	return filepath.Join(filepath.Dir(ex), "..", relPath), nil
}

// getFilePaths return full file paths in given directory with
// matching file extensions
func getFilePaths(dir string, ext string) ([]string, error) {
	files, err := ioutil.ReadDir(dir)
	if err != nil {
		return nil, err
	}
	extension := ext
	// if extension has been provided without '.' prefix, add one
	if filepath.Ext(ext) == "" {
		extension = fmt.Sprintf(".%s", ext)
	}
	var matchingFiles []string
	for _, file := range files {
		if filepath.Ext(file.Name()) == extension {
			matchingFiles = append(
				matchingFiles,
				fmt.Sprintf("%s/%s", dir, file.Name()))
		}
	}
	return matchingFiles, nil
}

// ControlService type is the data container for the service.
type ControlService struct {
	Storage
	logger             *log.Logger
	storageInitialised bool
	SupportedFeatures  FeatureMap
	NvmeNamespaces     NsMap
	NvmeControllers    CtrlrMap
}

// loadInitData retrieves initial data from file.
func (s *ControlService) loadInitData(filePath string) error {
	s.SupportedFeatures = make(FeatureMap)
	file, err := ioutil.ReadFile(filePath)
	if err != nil {
		return err
	}
	var features []*pb.Feature
	if err := json.Unmarshal(file, &features); err != nil {
		return err
	}
	for _, f := range features {
		s.SupportedFeatures[f.Fname.Name] = f
	}
	return nil
}

// NewControlServer creates a new instance of our ControlServer struct.
func NewControlServer() *ControlService {
	logger := log.NewLogger()
	logger.SetLevel(log.Debug)

	s := &ControlService{
		Storage:            &NvmeStorage{Logger: logger},
		storageInitialised: false,
		logger:             logger,
	}

	// Retrieve absolute path of init DB file and load data
	dbAbsPath, err := getAbsInstallPath(jsonDBRelPath)
	if err != nil {
		panic(err)
	}

	if err := s.loadInitData(dbAbsPath); err != nil {
		panic(err)
	}

	return s
}
