package com.intel.daos;

/**
 * This enum corresponds to mode for opening object in C API.
 */
public enum DaosObjectMode {

  // Shared read
  DAOS_OO_RO(1 << 1),

  // Shared read & write, no cache for write
  DAOS_OO_RW(1 << 2),

  // Exclusive write, data can be cached
  DAOS_OO_EXCL(1 << 3),

  // Random I/O
  DAOS_OO_IO_RAND(1 << 4),

  // Sequential I/O
  DAOS_OO_IO_SEQ(1 << 5);

  private final int value;

  DaosObjectMode(int value) {
    this.value = value;
  }

  public int getValue() {
    return value;
  }
}
