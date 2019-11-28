package com.intel.daos;

/**
 * This enum corresponds to mode for opening pool in C API.
 */
public enum DaosPoolMode {
  DAOS_PC_RO(1),
  DAOS_PC_RW(2),
  DAOS_PC_EX(4);

  private final int value;

  DaosPoolMode(int value) {
    this.value = value;
  }

  public int getValue() {
    return value;
  }
}
