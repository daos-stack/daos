/*
 * (C) Copyright 2018-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

package io.daos;

/**
 * Type of DAOS object.
 */
public enum DaosObjectType {
  OC_UNKNOWN,
  /**
   * Object classes with no data protection.
   * NB: The first 50 IDs are reserved for backward compatibility.
   */
  OC_BACK_COMPAT,
  /**
   * Single shard object.
   */
  OC_TINY,
  /**
   * Object with small number of shards.
   * Number of shards of the class is chosen by DAOS based on the
   * current size of the pool.
   */
  OC_SMALL,
  /**
   * Object with large number of shards.
   * Number of shards of the class is chosen by DAOS based on the
   * current size of the pool.
   */
  OC_LARGE,
  /**
   * Object with maximum number of shards.
   * Number of shards of the class is chosen by DAOS based on the
   * current size of the pool.
   */
  OC_MAX,

  /**
   * object classes protected by replication.
   */

  /**
   * Tiny object protected by replication.
   * This object class has one redundancy group.
   */
  OC_RP_TINY,
  /**
   * Replicated object with small number of redundancy groups.
   * Number of redundancy groups of the class is chosen by DAOS
   * based on the current size of the pool.
   */
  OC_RP_SMALL,
  /**
   * Replicated object with large number of redundancy groups.
   * Number of redundancy groups of the class is chosen by DAOS
   * based on the current size of the pool.
   */
  OC_RP_LARGE,
  /**
   * Replicated object with maximum number of redundancy groups.
   * Number of redundancy groups of the class is chosen by DAOS
   * based on the current size of the pool.
   */
  OC_RP_MAX,

  /**
   * Object classes protected by replication which supports Scalable.
   * Fetch (SF)
   * SF classes have more replicas, so they are slower on update, but more
   * scalable on fetch because they have more replicas to serve fetches.
   */

  /**
   * Tiny object protected by replication.
   * This object class has one redundancy group.
   */
  OC_RP_SF_TINY,
  /**
   * (SF) Replicated object with small number of redundancy groups.
   * Number of redundancy groups of the class is chosen by DAOS
   * based on the current size of the pool.
   */
  OC_RP_SF_SMALL,
  /**
   * (SF) Replicated object with large number of redundancy groups.
   * Number of redundancy groups of the class is chosen by DAOS
   * based on the current size of the pool.
   */
  OC_RP_SF_LARGE,
  /**
   * (SF) Replicated object with maximum number of redundancy groups.
   * Number of redundancy groups of the class is chosen by DAOS
   * based on the current size of the pool.
   */
  OC_RP_SF_MAX,

  /**
   * Replicated object class which is extremely scalable for fetch.
   * It has many replicas so it is very slow for update.
   */
  OC_RP_XSF,

  /**
   * Object classes protected by erasure code.
   */

  /**
   * Tiny object protected by EC
   * This object class has one redundancy group.
   */
  OC_EC_TINY,
  /**
   * EC object with small number of redundancy groups.
   * Number of redundancy groups of the class is chosen by DAOS
   * based on the current size of the pool.
   */
  OC_EC_SMALL,
  /**
   * EC object with large number of redundancy groups.
   * Number of redundancy groups of the class is chosen by DAOS
   * based on the current size of the pool.
   */
  OC_EC_LARGE,
  /**
   * EC object with maximum number of redundancy groups.
   * Number of redundancy groups of the class is chosen by DAOS
   * based on the current size of the pool.
   */
  OC_EC_MAX,

  /**
   * Object classes with explicit layout.
   */

  /**
   * Object classes with explicit layout but no data protection.
   * Examples:
   * S1 : shards=1, S2 means shards=2, ...
   * SX : spreading across all targets within the pool.
   */
  OC_S1,
  OC_S2,
  OC_S4,
  OC_S8,
  OC_S16,
  OC_S32,
  OC_S64,
  OC_S128,
  OC_S256,
  OC_S512,
  OC_S1K,
  OC_S2K,
  OC_S4K,
  OC_S8K,
  OC_SX,

  /**
   * Replicated object with explicit layout.
   * The first number is number of replicas, the number after G stands
   * for number of redundancy Groups.
   * Examples:
   * 2G1 : 2 replicas group=1
   * 3G2 : 3 replicas groups=2, ...
   * 8GX : 8 replicas, it spreads across all targets within the pool.
   */

  /**
   * 2-way replicated object classes.
   */
  OC_RP_2G1,
  OC_RP_2G2,
  OC_RP_2G4,
  OC_RP_2G8,
  OC_RP_2G16,
  OC_RP_2G32,
  OC_RP_2G64,
  OC_RP_2G128,
  OC_RP_2G256,
  OC_RP_2G512,
  OC_RP_2G1K,
  OC_RP_2G2K,
  OC_RP_2G4K,
  OC_RP_2G8K,
  OC_RP_2GX,

  /**
   * 3-way replicated object classes.
   */
  OC_RP_3G1,
  OC_RP_3G2,
  OC_RP_3G4,
  OC_RP_3G8,
  OC_RP_3G16,
  OC_RP_3G32,
  OC_RP_3G64,
  OC_RP_3G128,
  OC_RP_3G256,
  OC_RP_3G512,
  OC_RP_3G1K,
  OC_RP_3G2K,
  OC_RP_3G4K,
  OC_RP_3G8K,
  OC_RP_3GX,

  /**
   * 8-way replicated object classes.
   */
  OC_RP_8G1,
  OC_RP_8G2,
  OC_RP_8G4,
  OC_RP_8G8,
  OC_RP_8G16,
  OC_RP_8G32,
  OC_RP_8G64,
  OC_RP_8G128,
  OC_RP_8G256,
  OC_RP_8G512,
  OC_RP_8G1K,
  OC_RP_8G2K,
  OC_RP_8G4K,
  OC_RP_8G8K,
  OC_RP_8GX,

  /**
   * Erasure coded object with explicit layout
   * - the first number is data cells number within a redundancy group
   * - the number after P is parity cells number within a redundancy group
   * - the number after G is number of redundancy Groups.
   * Examples:
   * - 2P1G1: 2+1 EC object with one redundancy group
   * - 4P2G8: 4+2 EC object with 8 redundancy groups
   * - 8P2G2: 8+2 EC object with 2 redundancy groups
   * - 16P2GX: 16+2 EC object spreads across all targets within the pool.
   */

  /**
   * EC 2+1 object classes.
   */
  OC_EC_2P1G1,
  OC_EC_2P1G2,
  OC_EC_2P1G4,
  OC_EC_2P1G8,
  OC_EC_2P1G16,
  OC_EC_2P1G32,
  OC_EC_2P1G64,
  OC_EC_2P1G128,
  OC_EC_2P1G256,
  OC_EC_2P1G512,
  OC_EC_2P1G1K,
  OC_EC_2P1G2K,
  OC_EC_2P1G4K,
  OC_EC_2P1G8K,
  OC_EC_2P1GX,

  /**
   * EC 2+2 object classes.
   */
  OC_EC_2P2G1,
  OC_EC_2P2G2,
  OC_EC_2P2G4,
  OC_EC_2P2G8,
  OC_EC_2P2G16,
  OC_EC_2P2G32,
  OC_EC_2P2G64,
  OC_EC_2P2G128,
  OC_EC_2P2G256,
  OC_EC_2P2G512,
  OC_EC_2P2G1K,
  OC_EC_2P2G2K,
  OC_EC_2P2G4K,
  OC_EC_2P2G8K,
  OC_EC_2P2GX,

  /**
   * EC 4+1 object classes.
   */
  OC_EC_4P1G1,
  OC_EC_4P1G2,
  OC_EC_4P1G4,
  OC_EC_4P1G8,
  OC_EC_4P1G16,
  OC_EC_4P1G32,
  OC_EC_4P1G64,
  OC_EC_4P1G128,
  OC_EC_4P1G256,
  OC_EC_4P1G512,
  OC_EC_4P1G1K,
  OC_EC_4P1G2K,
  OC_EC_4P1G4K,
  OC_EC_4P1G8K,
  OC_EC_4P1GX,

  /**
   * EC 4+2 object classes.
   */
  OC_EC_4P2G1,
  OC_EC_4P2G2,
  OC_EC_4P2G4,
  OC_EC_4P2G8,
  OC_EC_4P2G16,
  OC_EC_4P2G32,
  OC_EC_4P2G64,
  OC_EC_4P2G128,
  OC_EC_4P2G256,
  OC_EC_4P2G512,
  OC_EC_4P2G1K,
  OC_EC_4P2G2K,
  OC_EC_4P2G4K,
  OC_EC_4P2G8K,
  OC_EC_4P2GX,

  /**
   * EC 8+1 object classes.
   */
  OC_EC_8P1G1,
  OC_EC_8P1G2,
  OC_EC_8P1G4,
  OC_EC_8P1G8,
  OC_EC_8P1G16,
  OC_EC_8P1G32,
  OC_EC_8P1G64,
  OC_EC_8P1G128,
  OC_EC_8P1G256,
  OC_EC_8P1G512,
  OC_EC_8P1G1K,
  OC_EC_8P1G2K,
  OC_EC_8P1G4K,
  OC_EC_8P1G8K,
  OC_EC_8P1GX,

  /**
   * EC 8+2 object classes.
   */
  OC_EC_8P2G1,
  OC_EC_8P2G2,
  OC_EC_8P2G4,
  OC_EC_8P2G8,
  OC_EC_8P2G16,
  OC_EC_8P2G32,
  OC_EC_8P2G64,
  OC_EC_8P2G128,
  OC_EC_8P2G256,
  OC_EC_8P2G512,
  OC_EC_8P2G1K,
  OC_EC_8P2G2K,
  OC_EC_8P2G4K,
  OC_EC_8P2G8K,
  OC_EC_8P2GX,

  /**
   * EC 16+1 object classes.
   */
  OC_EC_16P1G1,
  OC_EC_16P1G2,
  OC_EC_16P1G4,
  OC_EC_16P1G8,
  OC_EC_16P1G16,
  OC_EC_16P1G32,
  OC_EC_16P1G64,
  OC_EC_16P1G128,
  OC_EC_16P1G256,
  OC_EC_16P1G512,
  OC_EC_16P1G1K,
  OC_EC_16P1G2K,
  OC_EC_16P1G4K,
  OC_EC_16P1G8K,
  OC_EC_16P1GX,

  /**
   * EC 16+2 object classes.
   */
  OC_EC_16P2G1,
  OC_EC_16P2G2,
  OC_EC_16P2G4,
  OC_EC_16P2G8,
  OC_EC_16P2G16,
  OC_EC_16P2G32,
  OC_EC_16P2G64,
  OC_EC_16P2G128,
  OC_EC_16P2G256,
  OC_EC_16P2G512,
  OC_EC_16P2G1K,
  OC_EC_16P2G2K,
  OC_EC_16P2G4K,
  OC_EC_16P2G8K,
  OC_EC_16P2GX,

  /**
   * Class ID equal or higher than this is reserved.
   */
  OC_RESERVED;

  public String nameWithoutOc() {
    return name().substring(3);
  }
}
