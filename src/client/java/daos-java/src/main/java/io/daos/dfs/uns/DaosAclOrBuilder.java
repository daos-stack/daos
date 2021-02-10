/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.dfs.uns;

public interface DaosAclOrBuilder extends
    // @@protoc_insertion_point(interface_extends:uns.DaosAcl)
    com.google.protobuf.MessageOrBuilder {

  /**
   * <code>uint32 ver = 1;</code>
   *
   * @return The ver.
   */
  int getVer();

  /**
   * <code>uint32 reserv = 2;</code>
   *
   * @return The reserv.
   */
  int getReserv();

  /**
   * <code>repeated .uns.DaosAce aces = 4;</code>
   */
  java.util.List<io.daos.dfs.uns.DaosAce>
      getAcesList();

  /**
   * <code>repeated .uns.DaosAce aces = 4;</code>
   */
  io.daos.dfs.uns.DaosAce getAces(int index);

  /**
   * <code>repeated .uns.DaosAce aces = 4;</code>
   */
  int getAcesCount();

  /**
   * <code>repeated .uns.DaosAce aces = 4;</code>
   */
  java.util.List<? extends io.daos.dfs.uns.DaosAceOrBuilder>
      getAcesOrBuilderList();

  /**
   * <code>repeated .uns.DaosAce aces = 4;</code>
   */
  io.daos.dfs.uns.DaosAceOrBuilder getAcesOrBuilder(
      int index);
}
