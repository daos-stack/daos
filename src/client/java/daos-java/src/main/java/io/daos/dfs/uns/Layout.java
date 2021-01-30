/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.dfs.uns;

/**
 * Protobuf enum {@code uns.Layout}
 */
public enum Layout
    implements com.google.protobuf.ProtocolMessageEnum {
  /**
   * <code>UNKNOWN = 0;</code>
   */
  UNKNOWN(0),
  /**
   * <code>POSIX = 1;</code>
   */
  POSIX(1),
  /**
   * <code>HDF5 = 2;</code>
   */
  HDF5(2),
  UNRECOGNIZED(-1),
  ;

  /**
   * <code>UNKNOWN = 0;</code>
   */
  public static final int UNKNOWN_VALUE = 0;
  /**
   * <code>POSIX = 1;</code>
   */
  public static final int POSIX_VALUE = 1;
  /**
   * <code>HDF5 = 2;</code>
   */
  public static final int HDF5_VALUE = 2;


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
  public static Layout valueOf(int value) {
    return forNumber(value);
  }

  /**
   * @param value The numeric wire value of the corresponding enum entry.
   * @return The enum associated with the given numeric wire value.
   */
  public static Layout forNumber(int value) {
    switch (value) {
      case 0:
        return UNKNOWN;
      case 1:
        return POSIX;
      case 2:
        return HDF5;
      default:
        return null;
    }
  }

  public static com.google.protobuf.Internal.EnumLiteMap<Layout>
      internalGetValueMap() {
    return internalValueMap;
  }

  private static final com.google.protobuf.Internal.EnumLiteMap<
      Layout> internalValueMap =
      new com.google.protobuf.Internal.EnumLiteMap<Layout>() {
        public Layout findValueByNumber(int number) {
          return Layout.forNumber(number);
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
    return io.daos.dfs.uns.DunsClasses.getDescriptor().getEnumTypes().get(1);
  }

  private static final Layout[] VALUES = values();

  public static Layout valueOf(
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

  private Layout(int value) {
    this.value = value;
  }

  // @@protoc_insertion_point(enum_scope:uns.Layout)
}
