#!/usr/bin/env python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import os
import tempfile
import shutil


class FileGenerator():
    def __init__(self, prefix=""):
        if prefix:
            temp_path = tempfile.mkdtemp(prefix=prefix)
        else:
            temp_path = tempfile.mkdtemp()
        self._mock_root = os.path.join(temp_path, "daos")

    def get_root(self):
        return self._mock_root

    def crete_mock_fs(self, files):
        print("Temp directory: {0}".format(self._mock_root))
        self._create_files(files)

    def _create_files(self, files):
        for file in files:
            if file.get("type", "unknown") == "dir":
                self._create_dirs(file.get("path", ""))
            if file.get("type", "unknown") == "file":
                self.generate_file(file.get("path", ""), file.get("size", 0))
            if file.get("type", "unknown") == "symlink":
                self._create_symlink(
                    file.get(
                        "path", ""), file.get(
                        "dest", ""))

    def _create_symlink(self, path, dest):
        target_path = os.path.join(self._mock_root, path)
        os.symlink(dest, target_path)

    def _create_dirs(self, path):
        target_path = os.path.join(self._mock_root, path)
        if not os.path.isdir(target_path):
            os.makedirs(target_path)

    def generate_file(self, file, size):
        file_path = "/".join(file.split("/")[0:-1])
        self._create_dirs(file_path)
        target_file = os.path.join(self._mock_root, file)
        with open(target_file, "wb") as f:
            f.seek(size - 1)
            data = bytearray([1])
            f.write(data)

    def clean(self):
        shutil.rmtree(self._mock_root)

    def __del__(self):
        self.clean()
