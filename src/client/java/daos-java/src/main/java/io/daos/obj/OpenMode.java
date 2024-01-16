/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.obj;

public enum OpenMode {
  // shared read
  DAOS_OO_RO(1 << 1),
  // shared read & write, no cache for write
  DAOS_OO_RW(1 << 2),
  // exclusive write, data can be cached
  DAOS_OO_EXCL(1 << 3),
  // unsupported: random I/O
  DAOS_OO_IO_RAND(1 << 4),
  // unsupported sequential I/O
  DAOS_OO_IO_SEQ(1 << 5);

  private int value;

  OpenMode(int value) {
    this.value = value;
  }

  public int getValue() {
    return value;
  }
}
