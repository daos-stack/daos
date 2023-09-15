package main

import (
	"encoding/xml"
	"flag"
	"fmt"
	"io/fs"
	"io/ioutil"
	"os"
	"path/filepath"
)

type TestSuites struct {
	XMLName    xml.Name    `xml:"testsuites"`
	TestSuites []TestSuite `xml:"testsuite"`
}

type TestSuite struct {
	XMLName   xml.Name   `xml:"testsuite"`
	Name      string     `xml:"name,attr"`
	Time      float32    `xml:"time,attr,omitempty"`
	Tests     int        `xml:"tests,attr"`
	Failures  int        `xml:"failures,attr"`
	Errors    int        `xml:"errors,attr"`
	Skipped   int        `xml:"skipped,attr"`
	TestCases []TestCase `xml:"testcase"`
}

type TestCase struct {
	XMLName   xml.Name `xml:"testcase"`
	ClassName string   `xml:"classname,attr,omitempty"`
	Name      string   `xml:"name,attr"`
	Time      float32  `xml:"time,attr"`
	Failure   *Failure `xml:"failure,omitempty"`
}

type Failure struct {
	XMLName xml.Name `xml:"failure"`
	Message string   `xml:"message,attr,omitempty"`
	Value   string   `xml:",chardata"`
}

func isXML(f fs.FileInfo) bool {
	switch {
	case f.IsDir():
		return false
	case filepath.Ext(f.Name()) != ".xml":
		return false
	}

	return true
}

func unmarshalFile(path string) (*TestSuites, error) {
	bytes, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read results file %q: %w", path, err)
	}

	testSuites := &TestSuites{}
	err = xml.Unmarshal(bytes, &testSuites)
	if err != nil {
		return nil, fmt.Errorf("failed to unmarshal results file %q: %w", path, err)
	}

	return testSuites, nil
}

func readResultsFiles(dir string) (*TestSuites, error) {
	files, err := ioutil.ReadDir(dir)
	if err != nil {
		return nil, fmt.Errorf("failed to read results from directory %q: err")
	}

	allTestSuites := &TestSuites{}

	for _, f := range files {
		if !isXML(f) {
			continue
		}

		ts, err := unmarshalFile(filepath.Join(dir, f.Name()))
		if err != nil {
			return nil, err
		}

		allTestSuites.TestSuites = append(allTestSuites.TestSuites, ts.TestSuites...)
	}

	return allTestSuites, nil
}

func marshalResults(testSuites *TestSuites, path string) error {
	bytes, err := xml.MarshalIndent(testSuites, "", "  ")
	if err != nil {
		return fmt.Errorf("failed to marshall results data to XML: %w", err)
	}

	err = ioutil.WriteFile(path, bytes, 0644)
	if err != nil {
		return fmt.Errorf("failed to write results data to file %q: %w", path, err)
	}

	return nil
}

func main() {
	resultsDir := flag.String("results-dir", "", "Directory containing the JUnit XML results files to coalesce")
	outputFile := flag.String("output-file", "", "Path where the coalesced results file should be written")
	flag.Parse()

	fmt.Printf("Coalescing JUnit results files in %q...\n", *resultsDir)

	testSuites, err := readResultsFiles(*resultsDir)
	if err != nil {
		fmt.Printf("Failed to read results files in directory %q: %v\n", *resultsDir, err)
		os.Exit(1)
	}

	err = marshalResults(testSuites, *outputFile)
	if err != nil {
		fmt.Printf("Failed to write results to path %q: %v\n", *outputFile, err)
		os.Exit(1)
	}

	fmt.Printf("Wrote coalesced results to %q\n", *outputFile)
}
