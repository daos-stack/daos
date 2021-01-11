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

public final class DunsClasses {
  private DunsClasses() {
  }

  public static void registerAllExtensions(
      com.google.protobuf.ExtensionRegistryLite registry) {
  }

  public static void registerAllExtensions(
      com.google.protobuf.ExtensionRegistry registry) {
    registerAllExtensions(
        (com.google.protobuf.ExtensionRegistryLite) registry);
  }

  static final com.google.protobuf.Descriptors.Descriptor
      internal_static_uns_DaosAce_descriptor;
  static final
      com.google.protobuf.GeneratedMessageV3.FieldAccessorTable
      internal_static_uns_DaosAce_fieldAccessorTable;
  static final com.google.protobuf.Descriptors.Descriptor
      internal_static_uns_DaosAcl_descriptor;
  static final
      com.google.protobuf.GeneratedMessageV3.FieldAccessorTable
      internal_static_uns_DaosAcl_fieldAccessorTable;
  static final com.google.protobuf.Descriptors.Descriptor
      internal_static_uns_Entry_descriptor;
  static final
      com.google.protobuf.GeneratedMessageV3.FieldAccessorTable
      internal_static_uns_Entry_fieldAccessorTable;
  static final com.google.protobuf.Descriptors.Descriptor
      internal_static_uns_Properties_descriptor;
  static final
      com.google.protobuf.GeneratedMessageV3.FieldAccessorTable
      internal_static_uns_Properties_fieldAccessorTable;
  static final com.google.protobuf.Descriptors.Descriptor
      internal_static_uns_DunsAttribute_descriptor;
  static final
      com.google.protobuf.GeneratedMessageV3.FieldAccessorTable
      internal_static_uns_DunsAttribute_fieldAccessorTable;

  public static com.google.protobuf.Descriptors.FileDescriptor
      getDescriptor() {
    return descriptor;
  }

  private static com.google.protobuf.Descriptors.FileDescriptor
      descriptor;

  static {
    java.lang.String[] descriptorData = {
        "\n\023DunsAttribute.proto\022\003uns\"\310\001\n\007DaosAce\022\024" +
            "\n\014access_types\030\001 \001(\r\022\026\n\016principal_type\030\002" +
            " \001(\r\022\025\n\rprincipal_len\030\003 \001(\r\022\024\n\014access_fl" +
            "ags\030\004 \001(\r\022\020\n\010reserved\030\005 \001(\r\022\023\n\013allow_per" +
            "ms\030\006 \001(\r\022\023\n\013audit_perms\030\007 \001(\r\022\023\n\013alarm_p" +
            "erms\030\010 \001(\r\022\021\n\tprincipal\030\t \001(\t\"B\n\007DaosAcl" +
            "\022\013\n\003ver\030\001 \001(\r\022\016\n\006reserv\030\002 \001(\r\022\032\n\004aces\030\004 " +
            "\003(\0132\014.uns.DaosAce\"{\n\005Entry\022\033\n\004type\030\001 \001(\016" +
            "2\r.uns.PropType\022\020\n\010reserved\030\002 \001(\r\022\r\n\003val" +
            "\030\003 \001(\004H\000\022\r\n\003str\030\004 \001(\tH\000\022\034\n\004pval\030\005 \001(\0132\014." +
            "uns.DaosAclH\000B\007\n\005value\";\n\nProperties\022\020\n\010" +
            "reserved\030\001 \001(\r\022\033\n\007entries\030\002 \003(\0132\n.uns.En" +
            "try\"\325\001\n\rDunsAttribute\022\r\n\005puuid\030\001 \001(\t\022\r\n\005" +
            "cuuid\030\002 \001(\t\022 \n\013layout_type\030\003 \001(\0162\013.uns.L" +
            "ayout\022\023\n\013object_type\030\004 \001(\t\022\022\n\nchunk_size" +
            "\030\005 \001(\004\022\020\n\010rel_path\030\006 \001(\t\022\021\n\ton_lustre\030\007 " +
            "\001(\010\022#\n\nproperties\030\010 \001(\0132\017.uns.Properties" +
            "\022\021\n\tno_prefix\030\t \001(\010*\313\005\n\010PropType\022\024\n\020DAOS" +
            "_PROP_PO_MIN\020\000\022\026\n\022DAOS_PROP_PO_LABEL\020\001\022\024" +
            "\n\020DAOS_PROP_PO_ACL\020\002\022\031\n\025DAOS_PROP_PO_SPA" +
            "CE_RB\020\003\022\032\n\026DAOS_PROP_PO_SELF_HEAL\020\004\022\030\n\024D" +
            "AOS_PROP_PO_RECLAIM\020\005\022\026\n\022DAOS_PROP_PO_OW" +
            "NER\020\006\022\034\n\030DAOS_PROP_PO_OWNER_GROUP\020\007\022\031\n\025D" +
            "AOS_PROP_PO_SVC_LIST\020\010\022\024\n\020DAOS_PROP_PO_M" +
            "AX\020\t\022\025\n\020DAOS_PROP_CO_MIN\020\200 \022\027\n\022DAOS_PROP" +
            "_CO_LABEL\020\201 \022\035\n\030DAOS_PROP_CO_LAYOUT_TYPE" +
            "\020\202 \022\034\n\027DAOS_PROP_CO_LAYOUT_VER\020\203 \022\026\n\021DAO" +
            "S_PROP_CO_CSUM\020\204 \022!\n\034DAOS_PROP_CO_CSUM_C" +
            "HUNK_SIZE\020\205 \022$\n\037DAOS_PROP_CO_CSUM_SERVER" +
            "_VERIFY\020\206 \022\033\n\026DAOS_PROP_CO_REDUN_FAC\020\207 \022" +
            "\033\n\026DAOS_PROP_CO_REDUN_LVL\020\210 \022\036\n\031DAOS_PRO" +
            "P_CO_SNAPSHOT_MAX\020\211 \022\025\n\020DAOS_PROP_CO_ACL" +
            "\020\212 \022\032\n\025DAOS_PROP_CO_COMPRESS\020\213 \022\031\n\024DAOS_" +
            "PROP_CO_ENCRYPT\020\214 \022\027\n\022DAOS_PROP_CO_OWNER" +
            "\020\215 \022\035\n\030DAOS_PROP_CO_OWNER_GROUP\020\216 \022\025\n\020DA" +
            "OS_PROP_CO_MAX\020\217 **\n\006Layout\022\013\n\007UNKNOWN\020\000" +
            "\022\t\n\005POSIX\020\001\022\010\n\004HDF5\020\002B \n\017io.daos.dfs.uns" +
            "B\013DunsClassesP\001b\006proto3"
    };
    descriptor = com.google.protobuf.Descriptors.FileDescriptor
        .internalBuildGeneratedFileFrom(descriptorData,
            new com.google.protobuf.Descriptors.FileDescriptor[]{
            });
    internal_static_uns_DaosAce_descriptor =
        getDescriptor().getMessageTypes().get(0);
    internal_static_uns_DaosAce_fieldAccessorTable = new
        com.google.protobuf.GeneratedMessageV3.FieldAccessorTable(
        internal_static_uns_DaosAce_descriptor,
        new java.lang.String[]{"AccessTypes", "PrincipalType", "PrincipalLen", "AccessFlags", "Reserved",
            "AllowPerms", "AuditPerms", "AlarmPerms", "Principal",});
    internal_static_uns_DaosAcl_descriptor =
        getDescriptor().getMessageTypes().get(1);
    internal_static_uns_DaosAcl_fieldAccessorTable = new
        com.google.protobuf.GeneratedMessageV3.FieldAccessorTable(
        internal_static_uns_DaosAcl_descriptor,
        new java.lang.String[]{"Ver", "Reserv", "Aces",});
    internal_static_uns_Entry_descriptor =
        getDescriptor().getMessageTypes().get(2);
    internal_static_uns_Entry_fieldAccessorTable = new
        com.google.protobuf.GeneratedMessageV3.FieldAccessorTable(
        internal_static_uns_Entry_descriptor,
        new java.lang.String[]{"Type", "Reserved", "Val", "Str", "Pval", "Value",});
    internal_static_uns_Properties_descriptor =
        getDescriptor().getMessageTypes().get(3);
    internal_static_uns_Properties_fieldAccessorTable = new
        com.google.protobuf.GeneratedMessageV3.FieldAccessorTable(
        internal_static_uns_Properties_descriptor,
        new java.lang.String[]{"Reserved", "Entries",});
    internal_static_uns_DunsAttribute_descriptor =
        getDescriptor().getMessageTypes().get(4);
    internal_static_uns_DunsAttribute_fieldAccessorTable = new
        com.google.protobuf.GeneratedMessageV3.FieldAccessorTable(
        internal_static_uns_DunsAttribute_descriptor,
        new java.lang.String[]{"Puuid", "Cuuid", "LayoutType", "ObjectType", "ChunkSize", "RelPath", "OnLustre",
            "Properties", "NoPrefix",});
  }

  // @@protoc_insertion_point(outer_class_scope)
}
