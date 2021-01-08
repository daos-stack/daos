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
