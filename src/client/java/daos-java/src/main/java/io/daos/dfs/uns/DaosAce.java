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

/**
 * Protobuf type {@code uns.DaosAce}
 */
public final class DaosAce extends
    com.google.protobuf.GeneratedMessageV3 implements
    // @@protoc_insertion_point(message_implements:uns.DaosAce)
    DaosAceOrBuilder {
  private static final long serialVersionUID = 0L;

  // Use DaosAce.newBuilder() to construct.
  private DaosAce(com.google.protobuf.GeneratedMessageV3.Builder<?> builder) {
    super(builder);
  }

  private DaosAce() {
    principal_ = "";
  }

  @java.lang.Override
  @SuppressWarnings({"unused"})
  protected java.lang.Object newInstance(
      UnusedPrivateParameter unused) {
    return new DaosAce();
  }

  @java.lang.Override
  public com.google.protobuf.UnknownFieldSet
      getUnknownFields() {
    return this.unknownFields;
  }

  private DaosAce(
      com.google.protobuf.CodedInputStream input,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws com.google.protobuf.InvalidProtocolBufferException {
    this();
    if (extensionRegistry == null) {
      throw new java.lang.NullPointerException();
    }
    com.google.protobuf.UnknownFieldSet.Builder unknownFields =
        com.google.protobuf.UnknownFieldSet.newBuilder();
    try {
      boolean done = false;
      while (!done) {
        int tag = input.readTag();
        switch (tag) {
          case 0:
            done = true;
            break;
          case 8: {

            accessTypes_ = input.readUInt32();
            break;
          }
          case 16: {

            principalType_ = input.readUInt32();
            break;
          }
          case 24: {

            principalLen_ = input.readUInt32();
            break;
          }
          case 32: {

            accessFlags_ = input.readUInt32();
            break;
          }
          case 40: {

            reserved_ = input.readUInt32();
            break;
          }
          case 48: {

            allowPerms_ = input.readUInt32();
            break;
          }
          case 56: {

            auditPerms_ = input.readUInt32();
            break;
          }
          case 64: {

            alarmPerms_ = input.readUInt32();
            break;
          }
          case 74: {
            java.lang.String s = input.readStringRequireUtf8();

            principal_ = s;
            break;
          }
          default: {
            if (!parseUnknownField(
                input, unknownFields, extensionRegistry, tag)) {
              done = true;
            }
            break;
          }
        }
      }
    } catch (com.google.protobuf.InvalidProtocolBufferException e) {
      throw e.setUnfinishedMessage(this);
    } catch (java.io.IOException e) {
      throw new com.google.protobuf.InvalidProtocolBufferException(
          e).setUnfinishedMessage(this);
    } finally {
      this.unknownFields = unknownFields.build();
      makeExtensionsImmutable();
    }
  }

  public static com.google.protobuf.Descriptors.Descriptor
      getDescriptor() {
    return io.daos.dfs.uns.DunsClasses.internal_static_uns_DaosAce_descriptor;
  }

  @java.lang.Override
  protected com.google.protobuf.GeneratedMessageV3.FieldAccessorTable
      internalGetFieldAccessorTable() {
    return io.daos.dfs.uns.DunsClasses.internal_static_uns_DaosAce_fieldAccessorTable
        .ensureFieldAccessorsInitialized(
            io.daos.dfs.uns.DaosAce.class, io.daos.dfs.uns.DaosAce.Builder.class);
  }

  public static final int ACCESS_TYPES_FIELD_NUMBER = 1;
  private int accessTypes_;

  /**
   * <code>uint32 access_types = 1;</code>
   *
   * @return The accessTypes.
   */
  public int getAccessTypes() {
    return accessTypes_;
  }

  public static final int PRINCIPAL_TYPE_FIELD_NUMBER = 2;
  private int principalType_;

  /**
   * <code>uint32 principal_type = 2;</code>
   *
   * @return The principalType.
   */
  public int getPrincipalType() {
    return principalType_;
  }

  public static final int PRINCIPAL_LEN_FIELD_NUMBER = 3;
  private int principalLen_;

  /**
   * <code>uint32 principal_len = 3;</code>
   *
   * @return The principalLen.
   */
  public int getPrincipalLen() {
    return principalLen_;
  }

  public static final int ACCESS_FLAGS_FIELD_NUMBER = 4;
  private int accessFlags_;

  /**
   * <code>uint32 access_flags = 4;</code>
   *
   * @return The accessFlags.
   */
  public int getAccessFlags() {
    return accessFlags_;
  }

  public static final int RESERVED_FIELD_NUMBER = 5;
  private int reserved_;

  /**
   * <code>uint32 reserved = 5;</code>
   *
   * @return The reserved.
   */
  public int getReserved() {
    return reserved_;
  }

  public static final int ALLOW_PERMS_FIELD_NUMBER = 6;
  private int allowPerms_;

  /**
   * <code>uint32 allow_perms = 6;</code>
   *
   * @return The allowPerms.
   */
  public int getAllowPerms() {
    return allowPerms_;
  }

  public static final int AUDIT_PERMS_FIELD_NUMBER = 7;
  private int auditPerms_;

  /**
   * <code>uint32 audit_perms = 7;</code>
   *
   * @return The auditPerms.
   */
  public int getAuditPerms() {
    return auditPerms_;
  }

  public static final int ALARM_PERMS_FIELD_NUMBER = 8;
  private int alarmPerms_;

  /**
   * <code>uint32 alarm_perms = 8;</code>
   *
   * @return The alarmPerms.
   */
  public int getAlarmPerms() {
    return alarmPerms_;
  }

  public static final int PRINCIPAL_FIELD_NUMBER = 9;
  private volatile java.lang.Object principal_;

  /**
   * <code>string principal = 9;</code>
   *
   * @return The principal.
   */
  public java.lang.String getPrincipal() {
    java.lang.Object ref = principal_;
    if (ref instanceof java.lang.String) {
      return (java.lang.String) ref;
    } else {
      com.google.protobuf.ByteString bs =
          (com.google.protobuf.ByteString) ref;
      java.lang.String s = bs.toStringUtf8();
      principal_ = s;
      return s;
    }
  }

  /**
   * <code>string principal = 9;</code>
   *
   * @return The bytes for principal.
   */
  public com.google.protobuf.ByteString
      getPrincipalBytes() {
    java.lang.Object ref = principal_;
    if (ref instanceof java.lang.String) {
      com.google.protobuf.ByteString b =
          com.google.protobuf.ByteString.copyFromUtf8(
              (java.lang.String) ref);
      principal_ = b;
      return b;
    } else {
      return (com.google.protobuf.ByteString) ref;
    }
  }

  private byte memoizedIsInitialized = -1;

  @java.lang.Override
  public boolean isInitialized() {
    byte isInitialized = memoizedIsInitialized;
    if (isInitialized == 1) return true;
    if (isInitialized == 0) return false;

    memoizedIsInitialized = 1;
    return true;
  }

  @java.lang.Override
  public void writeTo(com.google.protobuf.CodedOutputStream output)
      throws java.io.IOException {
    if (accessTypes_ != 0) {
      output.writeUInt32(1, accessTypes_);
    }
    if (principalType_ != 0) {
      output.writeUInt32(2, principalType_);
    }
    if (principalLen_ != 0) {
      output.writeUInt32(3, principalLen_);
    }
    if (accessFlags_ != 0) {
      output.writeUInt32(4, accessFlags_);
    }
    if (reserved_ != 0) {
      output.writeUInt32(5, reserved_);
    }
    if (allowPerms_ != 0) {
      output.writeUInt32(6, allowPerms_);
    }
    if (auditPerms_ != 0) {
      output.writeUInt32(7, auditPerms_);
    }
    if (alarmPerms_ != 0) {
      output.writeUInt32(8, alarmPerms_);
    }
    if (!getPrincipalBytes().isEmpty()) {
      com.google.protobuf.GeneratedMessageV3.writeString(output, 9, principal_);
    }
    unknownFields.writeTo(output);
  }

  @java.lang.Override
  public int getSerializedSize() {
    int size = memoizedSize;
    if (size != -1) return size;

    size = 0;
    if (accessTypes_ != 0) {
      size += com.google.protobuf.CodedOutputStream
          .computeUInt32Size(1, accessTypes_);
    }
    if (principalType_ != 0) {
      size += com.google.protobuf.CodedOutputStream
          .computeUInt32Size(2, principalType_);
    }
    if (principalLen_ != 0) {
      size += com.google.protobuf.CodedOutputStream
          .computeUInt32Size(3, principalLen_);
    }
    if (accessFlags_ != 0) {
      size += com.google.protobuf.CodedOutputStream
          .computeUInt32Size(4, accessFlags_);
    }
    if (reserved_ != 0) {
      size += com.google.protobuf.CodedOutputStream
          .computeUInt32Size(5, reserved_);
    }
    if (allowPerms_ != 0) {
      size += com.google.protobuf.CodedOutputStream
          .computeUInt32Size(6, allowPerms_);
    }
    if (auditPerms_ != 0) {
      size += com.google.protobuf.CodedOutputStream
          .computeUInt32Size(7, auditPerms_);
    }
    if (alarmPerms_ != 0) {
      size += com.google.protobuf.CodedOutputStream
          .computeUInt32Size(8, alarmPerms_);
    }
    if (!getPrincipalBytes().isEmpty()) {
      size += com.google.protobuf.GeneratedMessageV3.computeStringSize(9, principal_);
    }
    size += unknownFields.getSerializedSize();
    memoizedSize = size;
    return size;
  }

  @java.lang.Override
  public boolean equals(final java.lang.Object obj) {
    if (obj == this) {
      return true;
    }
    if (!(obj instanceof io.daos.dfs.uns.DaosAce)) {
      return super.equals(obj);
    }
    io.daos.dfs.uns.DaosAce other = (io.daos.dfs.uns.DaosAce) obj;

    if (getAccessTypes()
        != other.getAccessTypes()) return false;
    if (getPrincipalType()
        != other.getPrincipalType()) return false;
    if (getPrincipalLen()
        != other.getPrincipalLen()) return false;
    if (getAccessFlags()
        != other.getAccessFlags()) return false;
    if (getReserved()
        != other.getReserved()) return false;
    if (getAllowPerms()
        != other.getAllowPerms()) return false;
    if (getAuditPerms()
        != other.getAuditPerms()) return false;
    if (getAlarmPerms()
        != other.getAlarmPerms()) return false;
    if (!getPrincipal()
        .equals(other.getPrincipal())) return false;
    if (!unknownFields.equals(other.unknownFields)) return false;
    return true;
  }

  @java.lang.Override
  public int hashCode() {
    if (memoizedHashCode != 0) {
      return memoizedHashCode;
    }
    int hash = 41;
    hash = (19 * hash) + getDescriptor().hashCode();
    hash = (37 * hash) + ACCESS_TYPES_FIELD_NUMBER;
    hash = (53 * hash) + getAccessTypes();
    hash = (37 * hash) + PRINCIPAL_TYPE_FIELD_NUMBER;
    hash = (53 * hash) + getPrincipalType();
    hash = (37 * hash) + PRINCIPAL_LEN_FIELD_NUMBER;
    hash = (53 * hash) + getPrincipalLen();
    hash = (37 * hash) + ACCESS_FLAGS_FIELD_NUMBER;
    hash = (53 * hash) + getAccessFlags();
    hash = (37 * hash) + RESERVED_FIELD_NUMBER;
    hash = (53 * hash) + getReserved();
    hash = (37 * hash) + ALLOW_PERMS_FIELD_NUMBER;
    hash = (53 * hash) + getAllowPerms();
    hash = (37 * hash) + AUDIT_PERMS_FIELD_NUMBER;
    hash = (53 * hash) + getAuditPerms();
    hash = (37 * hash) + ALARM_PERMS_FIELD_NUMBER;
    hash = (53 * hash) + getAlarmPerms();
    hash = (37 * hash) + PRINCIPAL_FIELD_NUMBER;
    hash = (53 * hash) + getPrincipal().hashCode();
    hash = (29 * hash) + unknownFields.hashCode();
    memoizedHashCode = hash;
    return hash;
  }

  public static io.daos.dfs.uns.DaosAce parseFrom(
      java.nio.ByteBuffer data)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data);
  }

  public static io.daos.dfs.uns.DaosAce parseFrom(
      java.nio.ByteBuffer data,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data, extensionRegistry);
  }

  public static io.daos.dfs.uns.DaosAce parseFrom(
      com.google.protobuf.ByteString data)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data);
  }

  public static io.daos.dfs.uns.DaosAce parseFrom(
      com.google.protobuf.ByteString data,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data, extensionRegistry);
  }

  public static io.daos.dfs.uns.DaosAce parseFrom(byte[] data)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data);
  }

  public static io.daos.dfs.uns.DaosAce parseFrom(
      byte[] data,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data, extensionRegistry);
  }

  public static io.daos.dfs.uns.DaosAce parseFrom(java.io.InputStream input)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseWithIOException(PARSER, input);
  }

  public static io.daos.dfs.uns.DaosAce parseFrom(
      java.io.InputStream input,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseWithIOException(PARSER, input, extensionRegistry);
  }

  public static io.daos.dfs.uns.DaosAce parseDelimitedFrom(java.io.InputStream input)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseDelimitedWithIOException(PARSER, input);
  }

  public static io.daos.dfs.uns.DaosAce parseDelimitedFrom(
      java.io.InputStream input,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseDelimitedWithIOException(PARSER, input, extensionRegistry);
  }

  public static io.daos.dfs.uns.DaosAce parseFrom(
      com.google.protobuf.CodedInputStream input)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseWithIOException(PARSER, input);
  }

  public static io.daos.dfs.uns.DaosAce parseFrom(
      com.google.protobuf.CodedInputStream input,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseWithIOException(PARSER, input, extensionRegistry);
  }

  @java.lang.Override
  public Builder newBuilderForType() {
    return newBuilder();
  }

  public static Builder newBuilder() {
    return DEFAULT_INSTANCE.toBuilder();
  }

  public static Builder newBuilder(io.daos.dfs.uns.DaosAce prototype) {
    return DEFAULT_INSTANCE.toBuilder().mergeFrom(prototype);
  }

  @java.lang.Override
  public Builder toBuilder() {
    return this == DEFAULT_INSTANCE
        ? new Builder() : new Builder().mergeFrom(this);
  }

  @java.lang.Override
  protected Builder newBuilderForType(
      com.google.protobuf.GeneratedMessageV3.BuilderParent parent) {
    Builder builder = new Builder(parent);
    return builder;
  }

  /**
   * Protobuf type {@code uns.DaosAce}
   */
  public static final class Builder extends
      com.google.protobuf.GeneratedMessageV3.Builder<Builder> implements
      // @@protoc_insertion_point(builder_implements:uns.DaosAce)
      io.daos.dfs.uns.DaosAceOrBuilder {
    public static final com.google.protobuf.Descriptors.Descriptor
        getDescriptor() {
      return io.daos.dfs.uns.DunsClasses.internal_static_uns_DaosAce_descriptor;
    }

    @java.lang.Override
    protected com.google.protobuf.GeneratedMessageV3.FieldAccessorTable
        internalGetFieldAccessorTable() {
      return io.daos.dfs.uns.DunsClasses.internal_static_uns_DaosAce_fieldAccessorTable
          .ensureFieldAccessorsInitialized(
              io.daos.dfs.uns.DaosAce.class, io.daos.dfs.uns.DaosAce.Builder.class);
    }

    // Construct using io.daos.dfs.uns.DaosAce.newBuilder()
    private Builder() {
      maybeForceBuilderInitialization();
    }

    private Builder(
        com.google.protobuf.GeneratedMessageV3.BuilderParent parent) {
      super(parent);
      maybeForceBuilderInitialization();
    }

    private void maybeForceBuilderInitialization() {
      if (com.google.protobuf.GeneratedMessageV3
          .alwaysUseFieldBuilders) {
      }
    }

    @java.lang.Override
    public Builder clear() {
      super.clear();
      accessTypes_ = 0;

      principalType_ = 0;

      principalLen_ = 0;

      accessFlags_ = 0;

      reserved_ = 0;

      allowPerms_ = 0;

      auditPerms_ = 0;

      alarmPerms_ = 0;

      principal_ = "";

      return this;
    }

    @java.lang.Override
    public com.google.protobuf.Descriptors.Descriptor
        getDescriptorForType() {
      return io.daos.dfs.uns.DunsClasses.internal_static_uns_DaosAce_descriptor;
    }

    @java.lang.Override
    public io.daos.dfs.uns.DaosAce getDefaultInstanceForType() {
      return io.daos.dfs.uns.DaosAce.getDefaultInstance();
    }

    @java.lang.Override
    public io.daos.dfs.uns.DaosAce build() {
      io.daos.dfs.uns.DaosAce result = buildPartial();
      if (!result.isInitialized()) {
        throw newUninitializedMessageException(result);
      }
      return result;
    }

    @java.lang.Override
    public io.daos.dfs.uns.DaosAce buildPartial() {
      io.daos.dfs.uns.DaosAce result = new io.daos.dfs.uns.DaosAce(this);
      result.accessTypes_ = accessTypes_;
      result.principalType_ = principalType_;
      result.principalLen_ = principalLen_;
      result.accessFlags_ = accessFlags_;
      result.reserved_ = reserved_;
      result.allowPerms_ = allowPerms_;
      result.auditPerms_ = auditPerms_;
      result.alarmPerms_ = alarmPerms_;
      result.principal_ = principal_;
      onBuilt();
      return result;
    }

    @java.lang.Override
    public Builder clone() {
      return super.clone();
    }

    @java.lang.Override
    public Builder setField(
        com.google.protobuf.Descriptors.FieldDescriptor field,
        java.lang.Object value) {
      return super.setField(field, value);
    }

    @java.lang.Override
    public Builder clearField(
        com.google.protobuf.Descriptors.FieldDescriptor field) {
      return super.clearField(field);
    }

    @java.lang.Override
    public Builder clearOneof(
        com.google.protobuf.Descriptors.OneofDescriptor oneof) {
      return super.clearOneof(oneof);
    }

    @java.lang.Override
    public Builder setRepeatedField(
        com.google.protobuf.Descriptors.FieldDescriptor field,
        int index, java.lang.Object value) {
      return super.setRepeatedField(field, index, value);
    }

    @java.lang.Override
    public Builder addRepeatedField(
        com.google.protobuf.Descriptors.FieldDescriptor field,
        java.lang.Object value) {
      return super.addRepeatedField(field, value);
    }

    @java.lang.Override
    public Builder mergeFrom(com.google.protobuf.Message other) {
      if (other instanceof io.daos.dfs.uns.DaosAce) {
        return mergeFrom((io.daos.dfs.uns.DaosAce) other);
      } else {
        super.mergeFrom(other);
        return this;
      }
    }

    public Builder mergeFrom(io.daos.dfs.uns.DaosAce other) {
      if (other == io.daos.dfs.uns.DaosAce.getDefaultInstance()) return this;
      if (other.getAccessTypes() != 0) {
        setAccessTypes(other.getAccessTypes());
      }
      if (other.getPrincipalType() != 0) {
        setPrincipalType(other.getPrincipalType());
      }
      if (other.getPrincipalLen() != 0) {
        setPrincipalLen(other.getPrincipalLen());
      }
      if (other.getAccessFlags() != 0) {
        setAccessFlags(other.getAccessFlags());
      }
      if (other.getReserved() != 0) {
        setReserved(other.getReserved());
      }
      if (other.getAllowPerms() != 0) {
        setAllowPerms(other.getAllowPerms());
      }
      if (other.getAuditPerms() != 0) {
        setAuditPerms(other.getAuditPerms());
      }
      if (other.getAlarmPerms() != 0) {
        setAlarmPerms(other.getAlarmPerms());
      }
      if (!other.getPrincipal().isEmpty()) {
        principal_ = other.principal_;
        onChanged();
      }
      this.mergeUnknownFields(other.unknownFields);
      onChanged();
      return this;
    }

    @java.lang.Override
    public final boolean isInitialized() {
      return true;
    }

    @java.lang.Override
    public Builder mergeFrom(
        com.google.protobuf.CodedInputStream input,
        com.google.protobuf.ExtensionRegistryLite extensionRegistry)
        throws java.io.IOException {
      io.daos.dfs.uns.DaosAce parsedMessage = null;
      try {
        parsedMessage = PARSER.parsePartialFrom(input, extensionRegistry);
      } catch (com.google.protobuf.InvalidProtocolBufferException e) {
        parsedMessage = (io.daos.dfs.uns.DaosAce) e.getUnfinishedMessage();
        throw e.unwrapIOException();
      } finally {
        if (parsedMessage != null) {
          mergeFrom(parsedMessage);
        }
      }
      return this;
    }

    private int accessTypes_;

    /**
     * <code>uint32 access_types = 1;</code>
     *
     * @return The accessTypes.
     */
    public int getAccessTypes() {
      return accessTypes_;
    }

    /**
     * <code>uint32 access_types = 1;</code>
     *
     * @param value The accessTypes to set.
     * @return This builder for chaining.
     */
    public Builder setAccessTypes(int value) {

      accessTypes_ = value;
      onChanged();
      return this;
    }

    /**
     * <code>uint32 access_types = 1;</code>
     *
     * @return This builder for chaining.
     */
    public Builder clearAccessTypes() {

      accessTypes_ = 0;
      onChanged();
      return this;
    }

    private int principalType_;

    /**
     * <code>uint32 principal_type = 2;</code>
     *
     * @return The principalType.
     */
    public int getPrincipalType() {
      return principalType_;
    }

    /**
     * <code>uint32 principal_type = 2;</code>
     *
     * @param value The principalType to set.
     * @return This builder for chaining.
     */
    public Builder setPrincipalType(int value) {

      principalType_ = value;
      onChanged();
      return this;
    }

    /**
     * <code>uint32 principal_type = 2;</code>
     *
     * @return This builder for chaining.
     */
    public Builder clearPrincipalType() {

      principalType_ = 0;
      onChanged();
      return this;
    }

    private int principalLen_;

    /**
     * <code>uint32 principal_len = 3;</code>
     *
     * @return The principalLen.
     */
    public int getPrincipalLen() {
      return principalLen_;
    }

    /**
     * <code>uint32 principal_len = 3;</code>
     *
     * @param value The principalLen to set.
     * @return This builder for chaining.
     */
    public Builder setPrincipalLen(int value) {

      principalLen_ = value;
      onChanged();
      return this;
    }

    /**
     * <code>uint32 principal_len = 3;</code>
     *
     * @return This builder for chaining.
     */
    public Builder clearPrincipalLen() {

      principalLen_ = 0;
      onChanged();
      return this;
    }

    private int accessFlags_;

    /**
     * <code>uint32 access_flags = 4;</code>
     *
     * @return The accessFlags.
     */
    public int getAccessFlags() {
      return accessFlags_;
    }

    /**
     * <code>uint32 access_flags = 4;</code>
     *
     * @param value The accessFlags to set.
     * @return This builder for chaining.
     */
    public Builder setAccessFlags(int value) {

      accessFlags_ = value;
      onChanged();
      return this;
    }

    /**
     * <code>uint32 access_flags = 4;</code>
     *
     * @return This builder for chaining.
     */
    public Builder clearAccessFlags() {

      accessFlags_ = 0;
      onChanged();
      return this;
    }

    private int reserved_;

    /**
     * <code>uint32 reserved = 5;</code>
     *
     * @return The reserved.
     */
    public int getReserved() {
      return reserved_;
    }

    /**
     * <code>uint32 reserved = 5;</code>
     *
     * @param value The reserved to set.
     * @return This builder for chaining.
     */
    public Builder setReserved(int value) {

      reserved_ = value;
      onChanged();
      return this;
    }

    /**
     * <code>uint32 reserved = 5;</code>
     *
     * @return This builder for chaining.
     */
    public Builder clearReserved() {

      reserved_ = 0;
      onChanged();
      return this;
    }

    private int allowPerms_;

    /**
     * <code>uint32 allow_perms = 6;</code>
     *
     * @return The allowPerms.
     */
    public int getAllowPerms() {
      return allowPerms_;
    }

    /**
     * <code>uint32 allow_perms = 6;</code>
     *
     * @param value The allowPerms to set.
     * @return This builder for chaining.
     */
    public Builder setAllowPerms(int value) {

      allowPerms_ = value;
      onChanged();
      return this;
    }

    /**
     * <code>uint32 allow_perms = 6;</code>
     *
     * @return This builder for chaining.
     */
    public Builder clearAllowPerms() {

      allowPerms_ = 0;
      onChanged();
      return this;
    }

    private int auditPerms_;

    /**
     * <code>uint32 audit_perms = 7;</code>
     *
     * @return The auditPerms.
     */
    public int getAuditPerms() {
      return auditPerms_;
    }

    /**
     * <code>uint32 audit_perms = 7;</code>
     *
     * @param value The auditPerms to set.
     * @return This builder for chaining.
     */
    public Builder setAuditPerms(int value) {

      auditPerms_ = value;
      onChanged();
      return this;
    }

    /**
     * <code>uint32 audit_perms = 7;</code>
     *
     * @return This builder for chaining.
     */
    public Builder clearAuditPerms() {

      auditPerms_ = 0;
      onChanged();
      return this;
    }

    private int alarmPerms_;

    /**
     * <code>uint32 alarm_perms = 8;</code>
     *
     * @return The alarmPerms.
     */
    public int getAlarmPerms() {
      return alarmPerms_;
    }

    /**
     * <code>uint32 alarm_perms = 8;</code>
     *
     * @param value The alarmPerms to set.
     * @return This builder for chaining.
     */
    public Builder setAlarmPerms(int value) {

      alarmPerms_ = value;
      onChanged();
      return this;
    }

    /**
     * <code>uint32 alarm_perms = 8;</code>
     *
     * @return This builder for chaining.
     */
    public Builder clearAlarmPerms() {

      alarmPerms_ = 0;
      onChanged();
      return this;
    }

    private java.lang.Object principal_ = "";

    /**
     * <code>string principal = 9;</code>
     *
     * @return The principal.
     */
    public java.lang.String getPrincipal() {
      java.lang.Object ref = principal_;
      if (!(ref instanceof java.lang.String)) {
        com.google.protobuf.ByteString bs =
            (com.google.protobuf.ByteString) ref;
        java.lang.String s = bs.toStringUtf8();
        principal_ = s;
        return s;
      } else {
        return (java.lang.String) ref;
      }
    }

    /**
     * <code>string principal = 9;</code>
     *
     * @return The bytes for principal.
     */
    public com.google.protobuf.ByteString
        getPrincipalBytes() {
      java.lang.Object ref = principal_;
      if (ref instanceof String) {
        com.google.protobuf.ByteString b =
            com.google.protobuf.ByteString.copyFromUtf8(
                (java.lang.String) ref);
        principal_ = b;
        return b;
      } else {
        return (com.google.protobuf.ByteString) ref;
      }
    }

    /**
     * <code>string principal = 9;</code>
     *
     * @param value The principal to set.
     * @return This builder for chaining.
     */
    public Builder setPrincipal(
        java.lang.String value) {
      if (value == null) {
        throw new NullPointerException();
      }

      principal_ = value;
      onChanged();
      return this;
    }

    /**
     * <code>string principal = 9;</code>
     *
     * @return This builder for chaining.
     */
    public Builder clearPrincipal() {

      principal_ = getDefaultInstance().getPrincipal();
      onChanged();
      return this;
    }

    /**
     * <code>string principal = 9;</code>
     *
     * @param value The bytes for principal to set.
     * @return This builder for chaining.
     */
    public Builder setPrincipalBytes(
        com.google.protobuf.ByteString value) {
      if (value == null) {
        throw new NullPointerException();
      }
      checkByteStringIsUtf8(value);

      principal_ = value;
      onChanged();
      return this;
    }

    @java.lang.Override
    public final Builder setUnknownFields(
        final com.google.protobuf.UnknownFieldSet unknownFields) {
      return super.setUnknownFields(unknownFields);
    }

    @java.lang.Override
    public final Builder mergeUnknownFields(
        final com.google.protobuf.UnknownFieldSet unknownFields) {
      return super.mergeUnknownFields(unknownFields);
    }


    // @@protoc_insertion_point(builder_scope:uns.DaosAce)
  }

  // @@protoc_insertion_point(class_scope:uns.DaosAce)
  private static final io.daos.dfs.uns.DaosAce DEFAULT_INSTANCE;

  static {
    DEFAULT_INSTANCE = new io.daos.dfs.uns.DaosAce();
  }

  public static io.daos.dfs.uns.DaosAce getDefaultInstance() {
    return DEFAULT_INSTANCE;
  }

  private static final com.google.protobuf.Parser<DaosAce>
      PARSER = new com.google.protobuf.AbstractParser<DaosAce>() {
    @java.lang.Override
    public DaosAce parsePartialFrom(
        com.google.protobuf.CodedInputStream input,
        com.google.protobuf.ExtensionRegistryLite extensionRegistry)
        throws com.google.protobuf.InvalidProtocolBufferException {
      return new DaosAce(input, extensionRegistry);
    }
  };

  public static com.google.protobuf.Parser<DaosAce> parser() {
    return PARSER;
  }

  @java.lang.Override
  public com.google.protobuf.Parser<DaosAce> getParserForType() {
    return PARSER;
  }

  @java.lang.Override
  public io.daos.dfs.uns.DaosAce getDefaultInstanceForType() {
    return DEFAULT_INSTANCE;
  }

}
