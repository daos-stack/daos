package main

import (
	"encoding/xml"
	"flag"
	"fmt"
	"os"
)

type TestSuites struct {
	XMLName  xml.Name `xml:"testsuites"`
	Disabled int      `xml:"disabled,attr"`
	Errors   int      `xml:"errors,attr"`
	Failures int      `xml:"failures,attr"`
	Tests    int      `xml:"tests,attr"`
}

func unmarshalFile(path string) (*TestSuites, error) {
	bytes, err := os.ReadFile(path)
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

func main() {
	resultsFileName := flag.String("file-name", "", "The file name of the NLT XML result.")
	flag.Parse()

	fmt.Printf("Reading %q\n", *resultsFileName)

	testSuites, err := unmarshalFile(*resultsFileName)
	if err != nil {
		fmt.Printf("Failed to read %q: %v\n", *resultsFileName, err)
		os.Exit(1)
	}

	fmt.Printf("Testsuites has %d failures\n", testSuites.Failures)

	if testSuites.Failures > 0 {
		os.Exit(1)
	}
}
