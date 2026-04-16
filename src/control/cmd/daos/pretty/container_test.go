//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"fmt"
	"regexp"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
)

// normalizeTimestamps replaces all timestamp strings with a placeholder to allow
// timestamp-agnostic comparisons in tests.
func normalizeTimestamps(s string) string {
	timestampRegex := regexp.MustCompile(`\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+ [+-]\d{4} \w+`)
	return timestampRegex.ReplaceAllString(s, "<TIMESTAMP>")
}

func TestPretty_PrintContainerInfo(t *testing.T) {
	testPoolUUID := test.MockPoolUUID()
	testContainerUUID := uuid.MustParse(test.MockUUID())
	testContainerLabel := "test-container"

	for name, tc := range map[string]struct {
		ci          *daos.ContainerInfo
		verbose     bool
		expPrintStr string
	}{
		"empty container info": {
			ci: &daos.ContainerInfo{},
			expPrintStr: fmt.Sprintf(`
  Container UUID : %s
  Container Type : unknown%s

`, uuid.Nil.String(), strings.Repeat(" ", len(uuid.Nil.String())-7)),
		},
		"minimal container info": {
			ci: &daos.ContainerInfo{
				ContainerUUID: testContainerUUID,
				Type:          daos.ContainerLayoutPOSIX,
			},
			expPrintStr: fmt.Sprintf(`
  Container UUID : %s
  Container Type : POSIX%s

`, testContainerUUID.String(), strings.Repeat(" ", len(testContainerUUID.String())-5)),
		},
		"container with label": {
			ci: &daos.ContainerInfo{
				ContainerUUID:  testContainerUUID,
				ContainerLabel: testContainerLabel,
				Type:           daos.ContainerLayoutHDF5,
			},
			expPrintStr: fmt.Sprintf(`
  Container UUID : %s
  Container Label: %s%s
  Container Type : HDF5%s

`, testContainerUUID.String(), testContainerLabel, strings.Repeat(" ", len(testContainerUUID.String())-len(testContainerLabel)),
				strings.Repeat(" ", len(testContainerUUID.String())-4)),
		},
		"verbose output with all fields": {
			ci: &daos.ContainerInfo{
				PoolUUID:         testPoolUUID,
				ContainerUUID:    testContainerUUID,
				ContainerLabel:   testContainerLabel,
				Type:             daos.ContainerLayoutPOSIX,
				RedundancyFactor: 2,
				NumHandles:       5,
				NumSnapshots:     3,
				OpenTime:         daos.HLC(12345),
				CloseModifyTime:  daos.HLC(67890),
				LatestSnapshot:   daos.HLC(11111),
			},
			verbose: true,
			expPrintStr: fmt.Sprintf(`
  Container UUID              : 00000001-0001-0001-0001-000000000001             
  Container Label             : %s                                   
  Container Type              : POSIX                                            
  Pool UUID                   : 00000001-0001-0001-0001-000000000001             
  Container redundancy factor : 2                                                
  Number of open handles      : 5                                                
  Latest open time            : <TIMESTAMP> (0x3039) 
  Latest close/modify time    : <TIMESTAMP> (0x10932)
  Number of snapshots         : 3                                                
  Latest Persistent Snapshot  : 0x2b67 (<TIMESTAMP>) 

`, testContainerLabel),
		},
		"verbose output without snapshot": {
			ci: &daos.ContainerInfo{
				PoolUUID:         testPoolUUID,
				ContainerUUID:    testContainerUUID,
				Type:             daos.ContainerLayoutDatabase,
				RedundancyFactor: 1,
				NumHandles:       0,
				NumSnapshots:     0,
				OpenTime:         daos.HLC(100),
				CloseModifyTime:  daos.HLC(200),
			},
			verbose: true,
			expPrintStr: `
  Container UUID              : 00000001-0001-0001-0001-000000000001          
  Container Type              : DATABASE                                      
  Pool UUID                   : 00000001-0001-0001-0001-000000000001          
  Container redundancy factor : 1                                             
  Number of open handles      : 0                                             
  Latest open time            : <TIMESTAMP> (0x64)
  Latest close/modify time    : <TIMESTAMP> (0xc8)
  Number of snapshots         : 0                                             

`,
		},
		"verbose output with POSIX attributes": {
			ci: &daos.ContainerInfo{
				PoolUUID:         testPoolUUID,
				ContainerUUID:    testContainerUUID,
				Type:             daos.ContainerLayoutPOSIX,
				RedundancyFactor: 2,
				NumHandles:       1,
				NumSnapshots:     0,
				OpenTime:         daos.HLC(1000),
				CloseModifyTime:  daos.HLC(2000),
				POSIXAttributes: &daos.POSIXAttributes{
					ChunkSize:       1048576,
					ObjectClass:     daos.ObjectClass(1),
					DirObjectClass:  daos.ObjectClass(2),
					FileObjectClass: daos.ObjectClass(3),
					Hints:           "test-hints",
				},
			},
			verbose: true,
			expPrintStr: `
  Container UUID              : 00000001-0001-0001-0001-000000000001           
  Container Type              : POSIX                                          
  Pool UUID                   : 00000001-0001-0001-0001-000000000001           
  Container redundancy factor : 2                                              
  Number of open handles      : 1                                              
  Latest open time            : <TIMESTAMP> (0x3e8)
  Latest close/modify time    : <TIMESTAMP> (0x7d0)
  Number of snapshots         : 0                                              
  Object Class                : 0x1                                            
  Dir Object Class            : 0x2                                            
  File Object Class           : 0x3                                            
  Hints                       : test-hints                                     
  Chunk Size                  : 1.0 MiB                                        

`,
		},
		"verbose output with partial POSIX attributes": {
			ci: &daos.ContainerInfo{
				PoolUUID:         testPoolUUID,
				ContainerUUID:    testContainerUUID,
				Type:             daos.ContainerLayoutPOSIX,
				RedundancyFactor: 1,
				NumHandles:       0,
				NumSnapshots:     0,
				OpenTime:         daos.HLC(500),
				CloseModifyTime:  daos.HLC(600),
				POSIXAttributes: &daos.POSIXAttributes{
					ChunkSize: 2097152,
				},
			},
			verbose: true,
			expPrintStr: `
  Container UUID              : 00000001-0001-0001-0001-000000000001           
  Container Type              : POSIX                                          
  Pool UUID                   : 00000001-0001-0001-0001-000000000001           
  Container redundancy factor : 1                                              
  Number of open handles      : 0                                              
  Latest open time            : <TIMESTAMP> (0x1f4)
  Latest close/modify time    : <TIMESTAMP> (0x258)
  Number of snapshots         : 0                                              
  Chunk Size                  : 2.0 MiB                                        

`,
		},
		"non-verbose with label and various types": {
			ci: &daos.ContainerInfo{
				ContainerUUID:  testContainerUUID,
				ContainerLabel: "spark-container",
				Type:           daos.ContainerLayoutSpark,
			},
			verbose: false,
			expPrintStr: fmt.Sprintf(`
  Container UUID : %s
  Container Label: spark-container%s
  Container Type : SPARK%s

`, testContainerUUID.String(), strings.Repeat(" ", len(testContainerUUID.String())-len("spark-container")),
				strings.Repeat(" ", len(testContainerUUID.String())-5)),
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			err := PrintContainerInfo(&bld, tc.ci, tc.verbose)
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}

			expected := normalizeTimestamps(strings.TrimLeft(tc.expPrintStr, "\n"))
			actual := normalizeTimestamps(bld.String())

			if diff := cmp.Diff(expected, actual); diff != "" {
				t.Fatalf("unexpected pretty-printed string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestPretty_PrintContainers(t *testing.T) {
	testPoolID := "test-pool-123"
	testUUID1 := uuid.MustParse(test.MockUUID())
	testUUID2 := uuid.MustParse("22222222-2222-2222-2222-222222222222")
	testUUID3 := uuid.MustParse("33333333-3333-3333-3333-333333333333")

	for name, tc := range map[string]struct {
		poolID      string
		containers  []*daos.ContainerInfo
		verbose     bool
		expPrintStr string
	}{
		"no containers": {
			poolID:     testPoolID,
			containers: []*daos.ContainerInfo{},
			expPrintStr: `No containers.
`,
		},
		"single container non-verbose": {
			poolID: testPoolID,
			containers: []*daos.ContainerInfo{
				{
					ContainerUUID:  testUUID1,
					ContainerLabel: "container-1",
					Type:           daos.ContainerLayoutPOSIX,
				},
			},
			verbose: false,
			expPrintStr: fmt.Sprintf(`Containers in pool %s:
  Label       
  -----       
  container-1 
`, testPoolID),
		},
		"single container verbose": {
			poolID: testPoolID,
			containers: []*daos.ContainerInfo{
				{
					ContainerUUID:  testUUID1,
					ContainerLabel: "container-1",
					Type:           daos.ContainerLayoutPOSIX,
				},
			},
			verbose: true,
			expPrintStr: fmt.Sprintf(`Containers in pool %s:
  Label       UUID                                 Layout 
  -----       ----                                 ------ 
  container-1 %s POSIX  
`, testPoolID, testUUID1.String()),
		},
		"multiple containers non-verbose": {
			poolID: testPoolID,
			containers: []*daos.ContainerInfo{
				{
					ContainerUUID:  testUUID1,
					ContainerLabel: "container-1",
					Type:           daos.ContainerLayoutPOSIX,
				},
				{
					ContainerUUID:  testUUID2,
					ContainerLabel: "container-2",
					Type:           daos.ContainerLayoutHDF5,
				},
				{
					ContainerUUID:  testUUID3,
					ContainerLabel: "",
					Type:           daos.ContainerLayoutDatabase,
				},
			},
			verbose: false,
			expPrintStr: fmt.Sprintf(`Containers in pool %s:
  Label       
  -----       
  container-1 
  container-2 
              
`, testPoolID),
		},
		"multiple containers verbose": {
			poolID: testPoolID,
			containers: []*daos.ContainerInfo{
				{
					ContainerUUID:  testUUID1,
					ContainerLabel: "container-1",
					Type:           daos.ContainerLayoutPOSIX,
				},
				{
					ContainerUUID:  testUUID2,
					ContainerLabel: "container-2",
					Type:           daos.ContainerLayoutHDF5,
				},
				{
					ContainerUUID:  testUUID3,
					ContainerLabel: "",
					Type:           daos.ContainerLayoutDatabase,
				},
			},
			verbose: true,
			expPrintStr: fmt.Sprintf(`Containers in pool %s:
  Label       UUID                                 Layout   
  -----       ----                                 ------   
  container-1 %s POSIX    
  container-2 %s HDF5     
              %s DATABASE 
`, testPoolID, testUUID1.String(), testUUID2.String(), testUUID3.String()),
		},
		"containers with various types": {
			poolID: testPoolID,
			containers: []*daos.ContainerInfo{
				{
					ContainerUUID:  testUUID1,
					ContainerLabel: "python-cont",
					Type:           daos.ContainerLayoutPython,
				},
				{
					ContainerUUID:  testUUID2,
					ContainerLabel: "spark-cont",
					Type:           daos.ContainerLayoutSpark,
				},
			},
			verbose: true,
			expPrintStr: fmt.Sprintf(`Containers in pool %s:
  Label       UUID                                 Layout 
  -----       ----                                 ------ 
  python-cont %s PYTHON 
  spark-cont  %s SPARK  
`, testPoolID, testUUID1.String(), testUUID2.String()),
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			PrintContainers(&bld, tc.poolID, tc.containers, tc.verbose)

			if diff := cmp.Diff(tc.expPrintStr, bld.String()); diff != "" {
				t.Fatalf("unexpected pretty-printed string (-want, +got):\n%s\n", diff)
			}
		})
	}
}
