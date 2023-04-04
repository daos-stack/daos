//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"archive/tar"
	"compress/gzip"
	"io"
	"os"
	"path/filepath"
)

// Archive and create the *tar.gz of the given folder.
func FolderCompress(src string, buf io.Writer) error {
	gzipWriter := gzip.NewWriter(buf)
	tarWriter := tar.NewWriter(gzipWriter)

	// Loop thorough the folder
	filepath.Walk(src, func(file string, fi os.FileInfo, err error) error {
		// generate tar File header
		header, err := tar.FileInfoHeader(fi, file)
		if err != nil {
			return err
		}

		// Convert filepath / to slash
		header.Name = filepath.ToSlash(file)

		// write the tar header
		if err := tarWriter.WriteHeader(header); err != nil {
			return err
		}

		// Write file content if it's not directory
		if !fi.IsDir() {
			data, err := os.Open(file)
			if err != nil {
				return err
			}
			if _, err := io.Copy(tarWriter, data); err != nil {
				return err
			}
		}
		return nil
	})

	// Create the tar
	if err := tarWriter.Close(); err != nil {
		return err
	}

	// Create the gzip
	if err := gzipWriter.Close(); err != nil {
		return err
	}

	return nil
}
