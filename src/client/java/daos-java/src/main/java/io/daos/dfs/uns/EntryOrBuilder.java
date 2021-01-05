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

public interface EntryOrBuilder extends
    // @@protoc_insertion_point(interface_extends:uns.Entry)
    com.google.protobuf.MessageOrBuilder {

  /**
   * <code>.uns.PropType type = 1;</code>
   *
   * @return The enum numeric value on the wire for type.
   */
  int getTypeValue();

  /**
   * <code>.uns.PropType type = 1;</code>
   *
   * @return The type.
   */
  io.daos.dfs.uns.PropType getType();

  /**
   * <code>uint32 reserved = 2;</code>
   *
   * @return The reserved.
   */
  int getReserved();

  /**
   * <code>uint64 val = 3;</code>
   *
   * @return The val.
   */
  long getVal();

  /**
   * <code>string str = 4;</code>
   *
   * @return The str.
   */
  java.lang.String getStr();

  /**
   * <code>string str = 4;</code>
   *
   * @return The bytes for str.
   */
  com.google.protobuf.ByteString
      getStrBytes();

  /**
   * <code>.uns.DaosAcl pval = 5;</code>
   *
   * @return Whether the pval field is set.
   */
  boolean hasPval();

  /**
   * <code>.uns.DaosAcl pval = 5;</code>
   *
   * @return The pval.
   */
  io.daos.dfs.uns.DaosAcl getPval();

  /**
   * <code>.uns.DaosAcl pval = 5;</code>
   */
  io.daos.dfs.uns.DaosAclOrBuilder getPvalOrBuilder();

  io.daos.dfs.uns.Entry.ValueCase getValueCase();
}
