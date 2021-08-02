/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.dfs.uns;

/**
 * <pre>
 * property types of pool and container
 * </pre>
 * <p>
 * Protobuf enum {@code uns.PropType}
 */
public enum PropType
    implements com.google.protobuf.ProtocolMessageEnum {
  /**
   * <pre>
   * pool property types
   * </pre>
   *
   * <code>DAOS_PROP_PO_MIN = 0;</code>
   */
  DAOS_PROP_PO_MIN(0),
  /**
   * <pre>
   * *
   * Label - a string that a user can associated with a pool.
   * default = ""
   * </pre>
   *
   * <code>DAOS_PROP_PO_LABEL = 1;</code>
   */
  DAOS_PROP_PO_LABEL(1),
  /**
   * <pre>
   * *
   * ACL: access control list for pool
   * An ordered list of access control entries detailing user and group
   * access privileges.
   * Expected to be in the order: Owner, User(s), Group(s), Everyone
   * </pre>
   *
   * <code>DAOS_PROP_PO_ACL = 2;</code>
   */
  DAOS_PROP_PO_ACL(2),
  /**
   * <pre>
   * *
   * Reserve space ratio: amount of space to be reserved on each target
   * for rebuild purpose. default = 0%.
   * </pre>
   *
   * <code>DAOS_PROP_PO_SPACE_RB = 3;</code>
   */
  DAOS_PROP_PO_SPACE_RB(3),
  /**
   * <pre>
   * *
   * Automatic/manual self-healing. default = auto
   * auto/manual exclusion
   * auto/manual rebuild
   * </pre>
   *
   * <code>DAOS_PROP_PO_SELF_HEAL = 4;</code>
   */
  DAOS_PROP_PO_SELF_HEAL(4),
  /**
   * <pre>
   * *
   * Space reclaim strategy = time|batched|snapshot. default = snapshot
   * time interval
   * batched commits
   * snapshot creation
   * </pre>
   *
   * <code>DAOS_PROP_PO_RECLAIM = 5;</code>
   */
  DAOS_PROP_PO_RECLAIM(5),
  /**
   * <pre>
   * *
   * The user who acts as the owner of the pool.
   * Format: user&#64;[domain]
   * </pre>
   *
   * <code>DAOS_PROP_PO_OWNER = 6;</code>
   */
  DAOS_PROP_PO_OWNER(6),
  /**
   * <pre>
   * *
   * The group that acts as the owner of the pool.
   * Format: group&#64;[domain]
   * </pre>
   *
   * <code>DAOS_PROP_PO_OWNER_GROUP = 7;</code>
   */
  DAOS_PROP_PO_OWNER_GROUP(7),
  /**
   * <pre>
   * *
   * The pool svc rank list.
   * </pre>
   *
   * <code>DAOS_PROP_PO_SVC_LIST = 8;</code>
   */
  DAOS_PROP_PO_SVC_LIST(8),
  /**
   * <code>DAOS_PROP_PO_MAX = 9;</code>
   */
  DAOS_PROP_PO_MAX(9),
  /**
   * <pre>
   * container property types
   * </pre>
   *
   * <code>DAOS_PROP_CO_MIN = 4096;</code>
   */
  DAOS_PROP_CO_MIN(4096),
  /**
   * <pre>
   * *
   * Label - a string that a user can associated with a container.
   * default = ""
   * </pre>
   *
   * <code>DAOS_PROP_CO_LABEL = 4097;</code>
   */
  DAOS_PROP_CO_LABEL(4097),
  /**
   * <pre>
   * *
   * Layout type: unknown, POSIX, HDF5, Python, ...
   * default value = DAOS_PROP_CO_LAYOUT_UNKNOWN
   * </pre>
   *
   * <code>DAOS_PROP_CO_LAYOUT_TYPE = 4098;</code>
   */
  DAOS_PROP_CO_LAYOUT_TYPE(4098),
  /**
   * <pre>
   * *
   * Layout version: specific to middleware for interop.
   * default = 1
   * </pre>
   *
   * <code>DAOS_PROP_CO_LAYOUT_VER = 4099;</code>
   */
  DAOS_PROP_CO_LAYOUT_VER(4099),
  /**
   * <pre>
   * *
   * Checksum on/off + checksum type (CRC16, CRC32, SHA-1 &amp; SHA-2).
   * default = DAOS_PROP_CO_CSUM_OFF
   * </pre>
   *
   * <code>DAOS_PROP_CO_CSUM = 4100;</code>
   */
  DAOS_PROP_CO_CSUM(4100),
  /**
   * <pre>
   * *
   * Checksum chunk size
   * default = 32K
   * </pre>
   *
   * <code>DAOS_PROP_CO_CSUM_CHUNK_SIZE = 4101;</code>
   */
  DAOS_PROP_CO_CSUM_CHUNK_SIZE(4101),
  /**
   * <pre>
   * *
   * Checksum verification on server. Value = ON/OFF
   * default = DAOS_PROP_CO_CSUM_SV_OFF
   * </pre>
   *
   * <code>DAOS_PROP_CO_CSUM_SERVER_VERIFY = 4102;</code>
   */
  DAOS_PROP_CO_CSUM_SERVER_VERIFY(4102),
  /**
   * <pre>
   * *
   * Redundancy factor:
   * RF(n): Container I/O restricted after n faults.
   * default = RF1 (DAOS_PROP_CO_REDUN_RF1)
   * </pre>
   *
   * <code>DAOS_PROP_CO_REDUN_FAC = 4103;</code>
   */
  DAOS_PROP_CO_REDUN_FAC(4103),
  /**
   * <pre>
   * *
   * Redundancy level: default fault domain level for placement.
   * default = rack (DAOS_PROP_CO_REDUN_NODE)
   * </pre>
   *
   * <code>DAOS_PROP_CO_REDUN_LVL = 4104;</code>
   */
  DAOS_PROP_CO_REDUN_LVL(4104),
  /**
   * <pre>
   * *
   * Maximum number of snapshots to retain.
   * </pre>
   *
   * <code>DAOS_PROP_CO_SNAPSHOT_MAX = 4105;</code>
   */
  DAOS_PROP_CO_SNAPSHOT_MAX(4105),
  /**
   * <pre>
   * *
   * ACL: access control list for container
   * An ordered list of access control entries detailing user and group
   * access privileges.
   * Expected to be in the order: Owner, User(s), Group(s), Everyone
   * </pre>
   *
   * <code>DAOS_PROP_CO_ACL = 4106;</code>
   */
  DAOS_PROP_CO_ACL(4106),
  /**
   * <pre>
   * * Compression on/off + compression type
   * </pre>
   *
   * <code>DAOS_PROP_CO_COMPRESS = 4107;</code>
   */
  DAOS_PROP_CO_COMPRESS(4107),
  /**
   * <pre>
   * * Encryption on/off + encryption type
   * </pre>
   *
   * <code>DAOS_PROP_CO_ENCRYPT = 4108;</code>
   */
  DAOS_PROP_CO_ENCRYPT(4108),
  /**
   * <pre>
   * *
   * The user who acts as the owner of the container.
   * Format: user&#64;[domain]
   * </pre>
   *
   * <code>DAOS_PROP_CO_OWNER = 4109;</code>
   */
  DAOS_PROP_CO_OWNER(4109),
  /**
   * <pre>
   * *
   * The group that acts as the owner of the container.
   * Format: group&#64;[domain]
   * </pre>
   *
   * <code>DAOS_PROP_CO_OWNER_GROUP = 4110;</code>
   */
  DAOS_PROP_CO_OWNER_GROUP(4110),
  /**
   * <code>DAOS_PROP_CO_MAX = 4111;</code>
   */
  DAOS_PROP_CO_MAX(4111),
  UNRECOGNIZED(-1),
  ;

  /**
   * <pre>
   * pool property types
   * </pre>
   *
   * <code>DAOS_PROP_PO_MIN = 0;</code>
   */
  public static final int DAOS_PROP_PO_MIN_VALUE = 0;
  /**
   * <pre>
   * *
   * Label - a string that a user can associated with a pool.
   * default = ""
   * </pre>
   *
   * <code>DAOS_PROP_PO_LABEL = 1;</code>
   */
  public static final int DAOS_PROP_PO_LABEL_VALUE = 1;
  /**
   * <pre>
   * *
   * ACL: access control list for pool
   * An ordered list of access control entries detailing user and group
   * access privileges.
   * Expected to be in the order: Owner, User(s), Group(s), Everyone
   * </pre>
   *
   * <code>DAOS_PROP_PO_ACL = 2;</code>
   */
  public static final int DAOS_PROP_PO_ACL_VALUE = 2;
  /**
   * <pre>
   * *
   * Reserve space ratio: amount of space to be reserved on each target
   * for rebuild purpose. default = 0%.
   * </pre>
   *
   * <code>DAOS_PROP_PO_SPACE_RB = 3;</code>
   */
  public static final int DAOS_PROP_PO_SPACE_RB_VALUE = 3;
  /**
   * <pre>
   * *
   * Automatic/manual self-healing. default = auto
   * auto/manual exclusion
   * auto/manual rebuild
   * </pre>
   *
   * <code>DAOS_PROP_PO_SELF_HEAL = 4;</code>
   */
  public static final int DAOS_PROP_PO_SELF_HEAL_VALUE = 4;
  /**
   * <pre>
   * *
   * Space reclaim strategy = time|batched|snapshot. default = snapshot
   * time interval
   * batched commits
   * snapshot creation
   * </pre>
   *
   * <code>DAOS_PROP_PO_RECLAIM = 5;</code>
   */
  public static final int DAOS_PROP_PO_RECLAIM_VALUE = 5;
  /**
   * <pre>
   * *
   * The user who acts as the owner of the pool.
   * Format: user&#64;[domain]
   * </pre>
   *
   * <code>DAOS_PROP_PO_OWNER = 6;</code>
   */
  public static final int DAOS_PROP_PO_OWNER_VALUE = 6;
  /**
   * <pre>
   * *
   * The group that acts as the owner of the pool.
   * Format: group&#64;[domain]
   * </pre>
   *
   * <code>DAOS_PROP_PO_OWNER_GROUP = 7;</code>
   */
  public static final int DAOS_PROP_PO_OWNER_GROUP_VALUE = 7;
  /**
   * <pre>
   * *
   * The pool svc rank list.
   * </pre>
   *
   * <code>DAOS_PROP_PO_SVC_LIST = 8;</code>
   */
  public static final int DAOS_PROP_PO_SVC_LIST_VALUE = 8;
  /**
   * <code>DAOS_PROP_PO_MAX = 9;</code>
   */
  public static final int DAOS_PROP_PO_MAX_VALUE = 9;
  /**
   * <pre>
   * container property types
   * </pre>
   *
   * <code>DAOS_PROP_CO_MIN = 4096;</code>
   */
  public static final int DAOS_PROP_CO_MIN_VALUE = 4096;
  /**
   * <pre>
   * *
   * Label - a string that a user can associated with a container.
   * default = ""
   * </pre>
   *
   * <code>DAOS_PROP_CO_LABEL = 4097;</code>
   */
  public static final int DAOS_PROP_CO_LABEL_VALUE = 4097;
  /**
   * <pre>
   * *
   * Layout type: unknown, POSIX, MPI-IO, HDF5, Apache Arrow, ...
   * default value = DAOS_PROP_CO_LAYOUT_UNKNOWN
   * </pre>
   *
   * <code>DAOS_PROP_CO_LAYOUT_TYPE = 4098;</code>
   */
  public static final int DAOS_PROP_CO_LAYOUT_TYPE_VALUE = 4098;
  /**
   * <pre>
   * *
   * Layout version: specific to middleware for interop.
   * default = 1
   * </pre>
   *
   * <code>DAOS_PROP_CO_LAYOUT_VER = 4099;</code>
   */
  public static final int DAOS_PROP_CO_LAYOUT_VER_VALUE = 4099;
  /**
   * <pre>
   * *
   * Checksum on/off + checksum type (CRC16, CRC32, SHA-1 &amp; SHA-2).
   * default = DAOS_PROP_CO_CSUM_OFF
   * </pre>
   *
   * <code>DAOS_PROP_CO_CSUM = 4100;</code>
   */
  public static final int DAOS_PROP_CO_CSUM_VALUE = 4100;
  /**
   * <pre>
   * *
   * Checksum chunk size
   * default = 32K
   * </pre>
   *
   * <code>DAOS_PROP_CO_CSUM_CHUNK_SIZE = 4101;</code>
   */
  public static final int DAOS_PROP_CO_CSUM_CHUNK_SIZE_VALUE = 4101;
  /**
   * <pre>
   * *
   * Checksum verification on server. Value = ON/OFF
   * default = DAOS_PROP_CO_CSUM_SV_OFF
   * </pre>
   *
   * <code>DAOS_PROP_CO_CSUM_SERVER_VERIFY = 4102;</code>
   */
  public static final int DAOS_PROP_CO_CSUM_SERVER_VERIFY_VALUE = 4102;
  /**
   * <pre>
   * *
   * Redundancy factor:
   * RF(n): Container I/O restricted after n faults.
   * default = RF1 (DAOS_PROP_CO_REDUN_RF1)
   * </pre>
   *
   * <code>DAOS_PROP_CO_REDUN_FAC = 4103;</code>
   */
  public static final int DAOS_PROP_CO_REDUN_FAC_VALUE = 4103;
  /**
   * <pre>
   * *
   * Redundancy level: default fault domain level for placement.
   * default = rack (DAOS_PROP_CO_REDUN_NODE)
   * </pre>
   *
   * <code>DAOS_PROP_CO_REDUN_LVL = 4104;</code>
   */
  public static final int DAOS_PROP_CO_REDUN_LVL_VALUE = 4104;
  /**
   * <pre>
   * *
   * Maximum number of snapshots to retain.
   * </pre>
   *
   * <code>DAOS_PROP_CO_SNAPSHOT_MAX = 4105;</code>
   */
  public static final int DAOS_PROP_CO_SNAPSHOT_MAX_VALUE = 4105;
  /**
   * <pre>
   * *
   * ACL: access control list for container
   * An ordered list of access control entries detailing user and group
   * access privileges.
   * Expected to be in the order: Owner, User(s), Group(s), Everyone
   * </pre>
   *
   * <code>DAOS_PROP_CO_ACL = 4106;</code>
   */
  public static final int DAOS_PROP_CO_ACL_VALUE = 4106;
  /**
   * <pre>
   * * Compression on/off + compression type
   * </pre>
   *
   * <code>DAOS_PROP_CO_COMPRESS = 4107;</code>
   */
  public static final int DAOS_PROP_CO_COMPRESS_VALUE = 4107;
  /**
   * <pre>
   * * Encryption on/off + encryption type
   * </pre>
   *
   * <code>DAOS_PROP_CO_ENCRYPT = 4108;</code>
   */
  public static final int DAOS_PROP_CO_ENCRYPT_VALUE = 4108;
  /**
   * <pre>
   * *
   * The user who acts as the owner of the container.
   * Format: user&#64;[domain]
   * </pre>
   *
   * <code>DAOS_PROP_CO_OWNER = 4109;</code>
   */
  public static final int DAOS_PROP_CO_OWNER_VALUE = 4109;
  /**
   * <pre>
   * *
   * The group that acts as the owner of the container.
   * Format: group&#64;[domain]
   * </pre>
   *
   * <code>DAOS_PROP_CO_OWNER_GROUP = 4110;</code>
   */
  public static final int DAOS_PROP_CO_OWNER_GROUP_VALUE = 4110;
  /**
   * <code>DAOS_PROP_CO_MAX = 4111;</code>
   */
  public static final int DAOS_PROP_CO_MAX_VALUE = 4111;


  public final int getNumber() {
    if (this == UNRECOGNIZED) {
      throw new java.lang.IllegalArgumentException(
          "Can't get the number of an unknown enum value.");
    }
    return value;
  }

  /**
   * @param value The numeric wire value of the corresponding enum entry.
   * @return The enum associated with the given numeric wire value.
   * @deprecated Use {@link #forNumber(int)} instead.
   */
  @java.lang.Deprecated
  public static PropType valueOf(int value) {
    return forNumber(value);
  }

  /**
   * @param value The numeric wire value of the corresponding enum entry.
   * @return The enum associated with the given numeric wire value.
   */
  public static PropType forNumber(int value) {
    switch (value) {
      case 0:
        return DAOS_PROP_PO_MIN;
      case 1:
        return DAOS_PROP_PO_LABEL;
      case 2:
        return DAOS_PROP_PO_ACL;
      case 3:
        return DAOS_PROP_PO_SPACE_RB;
      case 4:
        return DAOS_PROP_PO_SELF_HEAL;
      case 5:
        return DAOS_PROP_PO_RECLAIM;
      case 6:
        return DAOS_PROP_PO_OWNER;
      case 7:
        return DAOS_PROP_PO_OWNER_GROUP;
      case 8:
        return DAOS_PROP_PO_SVC_LIST;
      case 9:
        return DAOS_PROP_PO_MAX;
      case 4096:
        return DAOS_PROP_CO_MIN;
      case 4097:
        return DAOS_PROP_CO_LABEL;
      case 4098:
        return DAOS_PROP_CO_LAYOUT_TYPE;
      case 4099:
        return DAOS_PROP_CO_LAYOUT_VER;
      case 4100:
        return DAOS_PROP_CO_CSUM;
      case 4101:
        return DAOS_PROP_CO_CSUM_CHUNK_SIZE;
      case 4102:
        return DAOS_PROP_CO_CSUM_SERVER_VERIFY;
      case 4103:
        return DAOS_PROP_CO_REDUN_FAC;
      case 4104:
        return DAOS_PROP_CO_REDUN_LVL;
      case 4105:
        return DAOS_PROP_CO_SNAPSHOT_MAX;
      case 4106:
        return DAOS_PROP_CO_ACL;
      case 4107:
        return DAOS_PROP_CO_COMPRESS;
      case 4108:
        return DAOS_PROP_CO_ENCRYPT;
      case 4109:
        return DAOS_PROP_CO_OWNER;
      case 4110:
        return DAOS_PROP_CO_OWNER_GROUP;
      case 4111:
        return DAOS_PROP_CO_MAX;
      default:
        return null;
    }
  }

  public static com.google.protobuf.Internal.EnumLiteMap<PropType>
      internalGetValueMap() {
    return internalValueMap;
  }

  private static final com.google.protobuf.Internal.EnumLiteMap<
      PropType> internalValueMap =
      new com.google.protobuf.Internal.EnumLiteMap<PropType>() {
        public PropType findValueByNumber(int number) {
          return PropType.forNumber(number);
        }
      };

  public final com.google.protobuf.Descriptors.EnumValueDescriptor
      getValueDescriptor() {
    return getDescriptor().getValues().get(ordinal());
  }

  public final com.google.protobuf.Descriptors.EnumDescriptor
      getDescriptorForType() {
    return getDescriptor();
  }

  public static final com.google.protobuf.Descriptors.EnumDescriptor
      getDescriptor() {
    return io.daos.dfs.uns.DunsClasses.getDescriptor().getEnumTypes().get(0);
  }

  private static final PropType[] VALUES = values();

  public static PropType valueOf(
      com.google.protobuf.Descriptors.EnumValueDescriptor desc) {
    if (desc.getType() != getDescriptor()) {
      throw new java.lang.IllegalArgumentException(
          "EnumValueDescriptor is not for this type.");
    }
    if (desc.getIndex() == -1) {
      return UNRECOGNIZED;
    }
    return VALUES[desc.getIndex()];
  }

  private final int value;

  private PropType(int value) {
    this.value = value;
  }

  // @@protoc_insertion_point(enum_scope:uns.PropType)
}
