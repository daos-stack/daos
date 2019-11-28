package com.intel.daos;

/**
 * This enum corresponds to object features in C API.
 * It determines how object stores its keys.
 */
public enum DaosObjectFeature {

  // DKEY's are hashed and sorted in hashed order
  DAOS_OF_DKEY_HASHED(0),

  // AKEY's are hashed and sorted in hashed order
  DAOS_OF_AKEY_HASHED(0),

//  /** DKEY keys not hashed and sorted numerically.   Keys are accepted
//   *  in client's byte order and DAOS is responsible for correct behavior
//   */
//  DAOS_OF_DKEY_UINT64(1 << 0),

  // DKEY keys not hashed and sorted lexically
  DAOS_OF_DKEY_LEXICAL(1 << 1),

//  /** AKEY keys not hashed and sorted numerically.   Keys are accepted
//   *  in client's byte order and DAOS is responsible for correct behavior
//   */
//  DAOS_OF_AKEY_UINT64(1 << 2),

  // AKEY keys not hashed and sorted lexically
  DAOS_OF_AKEY_LEXICAL(1 << 3);

  private final int value;

  DaosObjectFeature(int value) {
    this.value = value;
  }

  public int getValue() {
    return value;
  }
}
