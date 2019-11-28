package com.intel.daos;

/**
 * This enum corresponds to mode for opening containers in C API.
 */
public enum DaosContainerMode {
  DAOS_COO_RO(1),
  DAOS_COO_RW(2),
  DAOS_COO_NOSLIP(4);

  private final int value;

  DaosContainerMode(int value) {
    this.value = value;
  }

  public int getValue() {
    return value;
  }
}
