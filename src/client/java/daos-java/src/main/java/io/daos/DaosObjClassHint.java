/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos;

public enum DaosObjClassHint {
  /** Flags to control OC Redundancy */
  DAOS_OCH_RDD_DEF(1 << 0),	/** Default - use RF prop */
  DAOS_OCH_RDD_NO(1 << 1),	/** No redundancy */
  DAOS_OCH_RDD_RP(1 << 2),	/** Replication */
  DAOS_OCH_RDD_EC(1 << 3),	/** Erasure Code */
  /** Flags to control OC Sharding */
  DAOS_OCH_SHD_DEF(1 << 4),	/** Default: Use MAX for array &
   * flat KV; 1 grp for others.
   */
  DAOS_OCH_SHD_TINY(1 << 5),	/** <= 4 grps */
  DAOS_OCH_SHD_REG(1 << 6),	/** max(128, 25%) */
  DAOS_OCH_SHD_HI(1 << 7),	/** max(256, 50%) */
  DAOS_OCH_SHD_EXT(1 << 8),	/** max(1024, 80%) */
  DAOS_OCH_SHD_MAX(1 << 9);	/** 100% */

  private final int id;

  DaosObjClassHint(int id) {
    this.id = id;
  }

  public int getId() {
    return id;
  }
}
