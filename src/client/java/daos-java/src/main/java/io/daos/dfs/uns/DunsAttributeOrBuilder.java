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

public interface DunsAttributeOrBuilder extends
    // @@protoc_insertion_point(interface_extends:uns.DunsAttribute)
    com.google.protobuf.MessageOrBuilder {

  /**
   * <code>string puuid = 1;</code>
   *
   * @return The puuid.
   */
  java.lang.String getPuuid();

  /**
   * <code>string puuid = 1;</code>
   *
   * @return The bytes for puuid.
   */
  com.google.protobuf.ByteString
      getPuuidBytes();

  /**
   * <code>string cuuid = 2;</code>
   *
   * @return The cuuid.
   */
  java.lang.String getCuuid();

  /**
   * <code>string cuuid = 2;</code>
   *
   * @return The bytes for cuuid.
   */
  com.google.protobuf.ByteString
      getCuuidBytes();

  /**
   * <code>.uns.Layout layout_type = 3;</code>
   *
   * @return The enum numeric value on the wire for layoutType.
   */
  int getLayoutTypeValue();

  /**
   * <code>.uns.Layout layout_type = 3;</code>
   *
   * @return The layoutType.
   */
  io.daos.dfs.uns.Layout getLayoutType();

  /**
   * <code>string object_type = 4;</code>
   *
   * @return The objectType.
   */
  java.lang.String getObjectType();

  /**
   * <code>string object_type = 4;</code>
   *
   * @return The bytes for objectType.
   */
  com.google.protobuf.ByteString
      getObjectTypeBytes();

  /**
   * <code>uint64 chunk_size = 5;</code>
   *
   * @return The chunkSize.
   */
  long getChunkSize();

  /**
   * <code>string rel_path = 6;</code>
   *
   * @return The relPath.
   */
  java.lang.String getRelPath();

  /**
   * <code>string rel_path = 6;</code>
   *
   * @return The bytes for relPath.
   */
  com.google.protobuf.ByteString
      getRelPathBytes();

  /**
   * <code>bool on_lustre = 7;</code>
   *
   * @return The onLustre.
   */
  boolean getOnLustre();

  /**
   * <code>.uns.Properties properties = 8;</code>
   *
   * @return Whether the properties field is set.
   */
  boolean hasProperties();

  /**
   * <code>.uns.Properties properties = 8;</code>
   *
   * @return The properties.
   */
  io.daos.dfs.uns.Properties getProperties();

  /**
   * <code>.uns.Properties properties = 8;</code>
   */
  io.daos.dfs.uns.PropertiesOrBuilder getPropertiesOrBuilder();

  /**
   * <code>bool no_prefix = 9;</code>
   *
   * @return The noPrefix.
   */
  boolean getNoPrefix();
}
