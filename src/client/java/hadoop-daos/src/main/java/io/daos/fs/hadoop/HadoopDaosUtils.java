/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop;

import java.io.FileNotFoundException;
import java.io.IOException;
import java.nio.file.DirectoryNotEmptyException;
import java.nio.file.FileAlreadyExistsException;
import java.nio.file.NotDirectoryException;

import io.daos.DaosIOException;

import org.apache.hadoop.fs.ParentNotDirectoryException;

/**
 * Utility class.
 */
public class HadoopDaosUtils {

  private static final int BASE_ENOENT = 2;
  private static final int DER_NONEXIST = -1005;
  private static final int DER_EXIST = -1004;
  private static final int BASE_EEXIST = 17;
  private static final int BASE_DIR_NO_EMPTY = 39;
  private static final int BASE_ENOTDIR = 20;

  /**
   * translate specific exception by DAOS error code.
   *
   * @param daosIOException
   * {@link DaosIOException} to be translated
   * @return hadoop exception
   */

  public static IOException translateException(DaosIOException daosIOException) {
    IOException ioe;
    int status = daosIOException.getErrorCode();
    switch (status) {
      case BASE_ENOENT :
      case DER_NONEXIST:
        ioe = new FileNotFoundException(daosIOException.getMessage());
        break;
      case DER_EXIST:
      case BASE_EEXIST:
        ioe = new FileAlreadyExistsException(daosIOException.getMessage());
        break;
      case BASE_DIR_NO_EMPTY:
        ioe = new DirectoryNotEmptyException(daosIOException.getMessage());
        break;
      case BASE_ENOTDIR:
        if (daosIOException.getMessage().contains("under parent directory")) {
          ioe = new ParentNotDirectoryException(daosIOException.getMessage());
        } else {
          ioe = new NotDirectoryException(daosIOException.getMessage());
        }
        break;
      default :
        ioe = daosIOException;
        break;
    }
    return ioe;
  }
}
