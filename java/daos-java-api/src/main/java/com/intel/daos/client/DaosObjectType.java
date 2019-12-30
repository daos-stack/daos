/*
 * (C) Copyright 2018-2019 Intel Corporation.
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

package com.intel.daos.client;

/**
 * Type of DAOS object.
 */
public enum DaosObjectType {
  OC_UNKNOWN(0),
  /**
   * Object classes with no data protection.
   * NB: The first 50 IDs are reserved for backward compatibility.
   */
  OC_BACK_COMPAT(50),
  /**
   * Single shard object.
   */
  OC_TINY(51),
  /**
   * Object with small number of shards.
   * Number of shards of the class is chosen by DAOS based on the
   * current size of the pool.
   */
  OC_SMALL(52),
  /**
   * Object with large number of shards.
   * Number of shards of the class is chosen by DAOS based on the
   * current size of the pool.
   */
  OC_LARGE(53),
  /**
   * Object with maximum number of shards.
   * Number of shards of the class is chosen by DAOS based on the
   * current size of the pool.
   */
  OC_MAX(54),

  /**
   * object classes protected by replication.
   */

  /**
   * Tiny object protected by replication.
   * This object class has one redundancy group.
   */
  OC_RP_TINY(60),
  /**
   * Replicated object with small number of redundancy groups.
   * Number of redundancy groups of the class is chosen by DAOS
   * based on the current size of the pool.
   */
  OC_RP_SMALL(61),
  /**
   * Replicated object with large number of redundancy groups.
   * Number of redundancy groups of the class is chosen by DAOS
   * based on the current size of the pool.
   */
  OC_RP_LARGE(62),
  /**
   * Replicated object with maximum number of redundancy groups.
   * Number of redundancy groups of the class is chosen by DAOS
   * based on the current size of the pool.
   */
  OC_RP_MAX(63),

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
  OC_RP_SF_TINY(70),
  /**
   * (SF) Replicated object with small number of redundancy groups.
   * Number of redundancy groups of the class is chosen by DAOS
   * based on the current size of the pool.
   */
  OC_RP_SF_SMALL(71),
  /**
   * (SF) Replicated object with large number of redundancy groups.
   * Number of redundancy groups of the class is chosen by DAOS
   * based on the current size of the pool.
   */
  OC_RP_SF_LARGE(72),
  /**
   * (SF) Replicated object with maximum number of redundancy groups.
   * Number of redundancy groups of the class is chosen by DAOS
   * based on the current size of the pool.
   */
  OC_RP_SF_MAX(73),

  /**
   * Replicated object class which is extremely scalable for fetch.
   * It has many replicas so it is very slow for update.
   */
  OC_RP_XSF(80),

  /**
   * Object classes protected by erasure code.
   */

  /**
   * Tiny object protected by EC
   * This object class has one redundancy group.
   */
  OC_EC_TINY(100),
  /**
   * EC object with small number of redundancy groups.
   * Number of redundancy groups of the class is chosen by DAOS
   * based on the current size of the pool.
   */
  OC_EC_SMALL(101),
  /**
   * EC object with large number of redundancy groups.
   * Number of redundancy groups of the class is chosen by DAOS
   * based on the current size of the pool.
   */
  OC_EC_LARGE(102),
  /**
   * EC object with maximum number of redundancy groups.
   * Number of redundancy groups of the class is chosen by DAOS
   * based on the current size of the pool.
   */
  OC_EC_MAX(103),

  /**
   * Object classes with explicit layout.
   */

  /**
   * Object classes with explicit layout but no data protection.
   * Examples:
   * S1 : shards=1, S2 means shards=2, ...
   * SX : spreading across all targets within the pool.
   */
  OC_S1(200),
  OC_S2(201),
  OC_S4(202),
  OC_S8(203),
  OC_S16(204),
  OC_S32(205),
  OC_S64(206),
  OC_S128(207),
  OC_S256(208),
  OC_S512(209),
  OC_S1K(210),
  OC_S2K(211),
  OC_S4K(212),
  OC_S8K(213),
  OC_SX(214),

  /**
   * Replicated object with explicit layout.
   * The first number is number of replicas, the number after G stands
   * for number of redundancy Groups.
   *
   * Examples:
   * 2G1 : 2 replicas group=1
   * 3G2 : 3 replicas groups=2, ...
   * 8GX : 8 replicas, it spreads across all targets within the pool.
   */

  /**
   * 2-way replicated object classes.
   */
  OC_RP_2G1(220),
  OC_RP_2G2(221),
  OC_RP_2G4(222),
  OC_RP_2G8(223),
  OC_RP_2G16(224),
  OC_RP_2G32(225),
  OC_RP_2G64(226),
  OC_RP_2G128(227),
  OC_RP_2G256(228),
  OC_RP_2G512(229),
  OC_RP_2G1K(230),
  OC_RP_2G2K(231),
  OC_RP_2G4K(232),
  OC_RP_2G8K(233),
  OC_RP_2GX(234),

  /**
   * 3-way replicated object classes.
   */
  OC_RP_3G1(240),
  OC_RP_3G2(241),
  OC_RP_3G4(242),
  OC_RP_3G8(243),
  OC_RP_3G16(244),
  OC_RP_3G32(245),
  OC_RP_3G64(246),
  OC_RP_3G128(247),
  OC_RP_3G256(248),
  OC_RP_3G512(249),
  OC_RP_3G1K(250),
  OC_RP_3G2K(251),
  OC_RP_3G4K(252),
  OC_RP_3G8K(253),
  OC_RP_3GX(254),

  /**
   * 8-way replicated object classes.
   */
  OC_RP_8G1(260),
  OC_RP_8G2(261),
  OC_RP_8G4(262),
  OC_RP_8G8(263),
  OC_RP_8G16(264),
  OC_RP_8G32(265),
  OC_RP_8G64(266),
  OC_RP_8G128(267),
  OC_RP_8G256(268),
  OC_RP_8G512(269),
  OC_RP_8G1K(270),
  OC_RP_8G2K(271),
  OC_RP_8G4K(272),
  OC_RP_8G8K(272),
  OC_RP_8GX(273),

  /**
   * Erasure coded object with explicit layout
   * - the first number is data cells number within a redundancy group
   * - the number after P is parity cells number within a redundancy group
   * - the number after G is number of redundancy Groups.
   *
   * Examples:
   * - 2P1G1: 2+1 EC object with one redundancy group
   * - 4P2G8: 4+2 EC object with 8 redundancy groups
   * - 8P2G2: 8+2 EC object with 2 redundancy groups
   * - 16P2GX: 16+2 EC object spreads across all targets within the pool.
   */

  /**
   * EC 2+1 object classes.
   */
  OC_EC_2P1G1(280),
  OC_EC_2P1G2(281),
  OC_EC_2P1G4(282),
  OC_EC_2P1G8(283),
  OC_EC_2P1G16(284),
  OC_EC_2P1G32(285),
  OC_EC_2P1G64(286),
  OC_EC_2P1G128(287),
  OC_EC_2P1G256(288),
  OC_EC_2P1G512(289),
  OC_EC_2P1G1K(290),
  OC_EC_2P1G2K(291),
  OC_EC_2P1G4K(292),
  OC_EC_2P1G8K(293),
  OC_EC_2P1GX(294),

  /**
   * EC 2+2 object classes.
   */
  OC_EC_2P2G1(300),
  OC_EC_2P2G2(301),
  OC_EC_2P2G4(302),
  OC_EC_2P2G8(303),
  OC_EC_2P2G16(304),
  OC_EC_2P2G32(305),
  OC_EC_2P2G64(306),
  OC_EC_2P2G128(307),
  OC_EC_2P2G256(308),
  OC_EC_2P2G512(309),
  OC_EC_2P2G1K(310),
  OC_EC_2P2G2K(311),
  OC_EC_2P2G4K(312),
  OC_EC_2P2G8K(313),
  OC_EC_2P2GX(314),

  /**
   * EC 4+1 object classes.
   */
  OC_EC_4P1G1(320),
  OC_EC_4P1G2(321),
  OC_EC_4P1G4(322),
  OC_EC_4P1G8(323),
  OC_EC_4P1G16(324),
  OC_EC_4P1G32(325),
  OC_EC_4P1G64(326),
  OC_EC_4P1G128(327),
  OC_EC_4P1G256(328),
  OC_EC_4P1G512(329),
  OC_EC_4P1G1K(330),
  OC_EC_4P1G2K(331),
  OC_EC_4P1G4K(332),
  OC_EC_4P1G8K(333),
  OC_EC_4P1GX(334),

  /**
   * EC 4+2 object classes.
   */
  OC_EC_4P2G1(340),
  OC_EC_4P2G2(341),
  OC_EC_4P2G4(342),
  OC_EC_4P2G8(343),
  OC_EC_4P2G16(344),
  OC_EC_4P2G32(345),
  OC_EC_4P2G64(346),
  OC_EC_4P2G128(347),
  OC_EC_4P2G256(348),
  OC_EC_4P2G512(349),
  OC_EC_4P2G1K(350),
  OC_EC_4P2G2K(351),
  OC_EC_4P2G4K(352),
  OC_EC_4P2G8K(353),
  OC_EC_4P2GX(354),

  /**
   * EC 8+1 object classes.
   */
  OC_EC_8P1G1(360),
  OC_EC_8P1G2(361),
  OC_EC_8P1G4(362),
  OC_EC_8P1G8(363),
  OC_EC_8P1G16(364),
  OC_EC_8P1G32(365),
  OC_EC_8P1G64(366),
  OC_EC_8P1G128(367),
  OC_EC_8P1G256(368),
  OC_EC_8P1G512(369),
  OC_EC_8P1G1K(370),
  OC_EC_8P1G2K(371),
  OC_EC_8P1G4K(372),
  OC_EC_8P1G8K(373),
  OC_EC_8P1GX(374),

  /**
   * EC 8+2 object classes.
   */
  OC_EC_8P2G1(380),
  OC_EC_8P2G2(381),
  OC_EC_8P2G4(382),
  OC_EC_8P2G8(383),
  OC_EC_8P2G16(384),
  OC_EC_8P2G32(385),
  OC_EC_8P2G64(386),
  OC_EC_8P2G128(387),
  OC_EC_8P2G256(388),
  OC_EC_8P2G512(389),
  OC_EC_8P2G1K(390),
  OC_EC_8P2G2K(391),
  OC_EC_8P2G4K(392),
  OC_EC_8P2G8K(393),
  OC_EC_8P2GX(394),

  /**
   * EC 16+1 object classes.
   */
  OC_EC_16P1G1(400),
  OC_EC_16P1G2(401),
  OC_EC_16P1G4(402),
  OC_EC_16P1G8(403),
  OC_EC_16P1G16(404),
  OC_EC_16P1G32(405),
  OC_EC_16P1G64(406),
  OC_EC_16P1G128(407),
  OC_EC_16P1G256(408),
  OC_EC_16P1G512(409),
  OC_EC_16P1G1K(410),
  OC_EC_16P1G2K(411),
  OC_EC_16P1G4K(412),
  OC_EC_16P1G8K(413),
  OC_EC_16P1GX(414),

  /**
   * EC 16+2 object classes.
   */
  OC_EC_16P2G1(420),
  OC_EC_16P2G2(421),
  OC_EC_16P2G4(422),
  OC_EC_16P2G8(423),
  OC_EC_16P2G16(424),
  OC_EC_16P2G32(425),
  OC_EC_16P2G64(426),
  OC_EC_16P2G128(427),
  OC_EC_16P2G256(428),
  OC_EC_16P2G512(429),
  OC_EC_16P2G1K(430),
  OC_EC_16P2G2K(431),
  OC_EC_16P2G4K(432),
  OC_EC_16P2G8K(433),
  OC_EC_16P2GX(434),

  /**
   * Class ID equal or higher than this is reserved.
   */
  OC_RESERVED(1 << 10);

  private int value;

  DaosObjectType(int value) {
    this.value = value;
  }

  public int getValue() {
    return this.value;
  }
}
