/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.dfs.uns;

public interface PropertiesOrBuilder extends
    // @@protoc_insertion_point(interface_extends:uns.Properties)
    com.google.protobuf.MessageOrBuilder {

  /**
   * <code>uint32 reserved = 1;</code>
   *
   * @return The reserved.
   */
  int getReserved();

  /**
   * <code>repeated .uns.Entry entries = 2;</code>
   */
  java.util.List<io.daos.dfs.uns.Entry>
      getEntriesList();

  /**
   * <code>repeated .uns.Entry entries = 2;</code>
   */
  io.daos.dfs.uns.Entry getEntries(int index);

  /**
   * <code>repeated .uns.Entry entries = 2;</code>
   */
  int getEntriesCount();

  /**
   * <code>repeated .uns.Entry entries = 2;</code>
   */
  java.util.List<? extends io.daos.dfs.uns.EntryOrBuilder>
      getEntriesOrBuilderList();

  /**
   * <code>repeated .uns.Entry entries = 2;</code>
   */
  io.daos.dfs.uns.EntryOrBuilder getEntriesOrBuilder(
      int index);
}
