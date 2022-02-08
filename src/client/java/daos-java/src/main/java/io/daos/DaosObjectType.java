/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos;

/**
 * DAOS object type
 */
public enum DaosObjectType {
  /** default object type, multi-level KV with hashed [ad]keys */
  DAOS_OT_MULTI_HASHED(0),

  /**
   * Object ID table created on snapshot
   */
  DAOS_OT_OIT(1),

  /** KV with uint64 dkeys */
  DAOS_OT_DKEY_UINT64(2),

  /** KV with uint64 akeys */
  DAOS_OT_AKEY_UINT64(3),

  /** multi-level KV with uint64 [ad]keys */
  DAOS_OT_MULTI_UINT64(4),

  /** KV with lexical dkeys */
  DAOS_OT_DKEY_LEXICAL(5),

  /** KV with lexical akeys */
  DAOS_OT_AKEY_LEXICAL(6),

  /** multi-level KV with lexical [ad]keys */
  DAOS_OT_MULTI_LEXICAL(7),

  /** flat KV (no akey) with hashed dkey */
  DAOS_OT_KV_HASHED(8),

  /** flat KV (no akey) with integer dkey */
  DAOS_OT_KV_UINT64(9),

  /** flat KV (no akey) with lexical dkey */
  DAOS_OT_KV_LEXICAL(10),

  /** Array with attributes stored in the DAOS object */
  DAOS_OT_ARRAY(11),

  /** Array with attributes provided by the user */
  DAOS_OT_ARRAY_ATTR(12),

  /** Byte Array with no metadata (eg DFS/POSIX) */
  DAOS_OT_ARRAY_BYTE(13);

  /**
   * reserved: Multi Dimensional Array
   * DAOS_OT_ARRAY_MD	(64,
   */

  /**
   * reserved: Block device
   * DAOS_OT_BDEV	(96,
   */

  private final int id;

  DaosObjectType(int id) {
    this.id = id;
  }

  public int getId() {
    return id;
  }
}
