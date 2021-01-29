/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.dfs.uns;

public interface DaosAceOrBuilder extends
      // @@protoc_insertion_point(interface_extends:uns.DaosAce)
      com.google.protobuf.MessageOrBuilder {

  /**
   * <code>uint32 access_types = 1;</code>
   *
   * @return The accessTypes.
   */
  int getAccessTypes();

  /**
   * <code>uint32 principal_type = 2;</code>
   *
   * @return The principalType.
   */
  int getPrincipalType();

  /**
   * <code>uint32 principal_len = 3;</code>
   *
   * @return The principalLen.
   */
  int getPrincipalLen();

  /**
   * <code>uint32 access_flags = 4;</code>
   *
   * @return The accessFlags.
   */
  int getAccessFlags();

  /**
   * <code>uint32 reserved = 5;</code>
   *
   * @return The reserved.
   */
  int getReserved();

  /**
   * <code>uint32 allow_perms = 6;</code>
   *
   * @return The allowPerms.
   */
  int getAllowPerms();

  /**
   * <code>uint32 audit_perms = 7;</code>
   *
   * @return The auditPerms.
   */
  int getAuditPerms();

  /**
   * <code>uint32 alarm_perms = 8;</code>
   *
   * @return The alarmPerms.
   */
  int getAlarmPerms();

  /**
   * <code>string principal = 9;</code>
   *
   * @return The principal.
   */
  java.lang.String getPrincipal();

  /**
   * <code>string principal = 9;</code>
   *
   * @return The bytes for principal.
   */
  com.google.protobuf.ByteString
      getPrincipalBytes();
}
