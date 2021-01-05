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
 * Protobuf type {@code uns.DunsAttribute}
 */
public final class DunsAttribute extends
    com.google.protobuf.GeneratedMessageV3 implements
    // @@protoc_insertion_point(message_implements:uns.DunsAttribute)
    DunsAttributeOrBuilder {
  private static final long serialVersionUID = 0L;

  // Use DunsAttribute.newBuilder() to construct.
  private DunsAttribute(com.google.protobuf.GeneratedMessageV3.Builder<?> builder) {
    super(builder);
  }

  private DunsAttribute() {
    puuid_ = "";
    cuuid_ = "";
    layoutType_ = 0;
    objectType_ = "";
    relPath_ = "";
  }

  @java.lang.Override
  @SuppressWarnings({"unused"})
  protected java.lang.Object newInstance(
      UnusedPrivateParameter unused) {
    return new DunsAttribute();
  }

  @java.lang.Override
  public final com.google.protobuf.UnknownFieldSet
      getUnknownFields() {
    return this.unknownFields;
  }

  private DunsAttribute(
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
          case 10: {
            java.lang.String s = input.readStringRequireUtf8();

            puuid_ = s;
            break;
          }
          case 18: {
            java.lang.String s = input.readStringRequireUtf8();

            cuuid_ = s;
            break;
          }
          case 24: {
            int rawValue = input.readEnum();

            layoutType_ = rawValue;
            break;
          }
          case 34: {
            java.lang.String s = input.readStringRequireUtf8();

            objectType_ = s;
            break;
          }
          case 40: {

            chunkSize_ = input.readUInt64();
            break;
          }
          case 50: {
            java.lang.String s = input.readStringRequireUtf8();

            relPath_ = s;
            break;
          }
          case 56: {

            onLustre_ = input.readBool();
            break;
          }
          case 66: {
            io.daos.dfs.uns.Properties.Builder subBuilder = null;
            if (properties_ != null) {
              subBuilder = properties_.toBuilder();
            }
            properties_ = input.readMessage(io.daos.dfs.uns.Properties.parser(), extensionRegistry);
            if (subBuilder != null) {
              subBuilder.mergeFrom(properties_);
              properties_ = subBuilder.buildPartial();
            }

            break;
          }
          case 72: {

            noPrefix_ = input.readBool();
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

  public static final com.google.protobuf.Descriptors.Descriptor
      getDescriptor() {
    return io.daos.dfs.uns.DunsClasses.internal_static_uns_DunsAttribute_descriptor;
  }

  @java.lang.Override
  protected com.google.protobuf.GeneratedMessageV3.FieldAccessorTable
      internalGetFieldAccessorTable() {
    return io.daos.dfs.uns.DunsClasses.internal_static_uns_DunsAttribute_fieldAccessorTable
        .ensureFieldAccessorsInitialized(
            io.daos.dfs.uns.DunsAttribute.class, io.daos.dfs.uns.DunsAttribute.Builder.class);
  }

  public static final int PUUID_FIELD_NUMBER = 1;
  private volatile java.lang.Object puuid_;

  /**
   * <code>string puuid = 1;</code>
   *
   * @return The puuid.
   */
  public java.lang.String getPuuid() {
    java.lang.Object ref = puuid_;
    if (ref instanceof java.lang.String) {
      return (java.lang.String) ref;
    } else {
      com.google.protobuf.ByteString bs =
          (com.google.protobuf.ByteString) ref;
      java.lang.String s = bs.toStringUtf8();
      puuid_ = s;
      return s;
    }
  }

  /**
   * <code>string puuid = 1;</code>
   *
   * @return The bytes for puuid.
   */
  public com.google.protobuf.ByteString
      getPuuidBytes() {
    java.lang.Object ref = puuid_;
    if (ref instanceof java.lang.String) {
      com.google.protobuf.ByteString b =
          com.google.protobuf.ByteString.copyFromUtf8(
              (java.lang.String) ref);
      puuid_ = b;
      return b;
    } else {
      return (com.google.protobuf.ByteString) ref;
    }
  }

  public static final int CUUID_FIELD_NUMBER = 2;
  private volatile java.lang.Object cuuid_;

  /**
   * <code>string cuuid = 2;</code>
   *
   * @return The cuuid.
   */
  public java.lang.String getCuuid() {
    java.lang.Object ref = cuuid_;
    if (ref instanceof java.lang.String) {
      return (java.lang.String) ref;
    } else {
      com.google.protobuf.ByteString bs =
          (com.google.protobuf.ByteString) ref;
      java.lang.String s = bs.toStringUtf8();
      cuuid_ = s;
      return s;
    }
  }

  /**
   * <code>string cuuid = 2;</code>
   *
   * @return The bytes for cuuid.
   */
  public com.google.protobuf.ByteString
      getCuuidBytes() {
    java.lang.Object ref = cuuid_;
    if (ref instanceof java.lang.String) {
      com.google.protobuf.ByteString b =
          com.google.protobuf.ByteString.copyFromUtf8(
              (java.lang.String) ref);
      cuuid_ = b;
      return b;
    } else {
      return (com.google.protobuf.ByteString) ref;
    }
  }

  public static final int LAYOUT_TYPE_FIELD_NUMBER = 3;
  private int layoutType_;

  /**
   * <code>.uns.Layout layout_type = 3;</code>
   *
   * @return The enum numeric value on the wire for layoutType.
   */
  public int getLayoutTypeValue() {
    return layoutType_;
  }

  /**
   * <code>.uns.Layout layout_type = 3;</code>
   *
   * @return The layoutType.
   */
  public io.daos.dfs.uns.Layout getLayoutType() {
    @SuppressWarnings("deprecation")
    io.daos.dfs.uns.Layout result = io.daos.dfs.uns.Layout.valueOf(layoutType_);
    return result == null ? io.daos.dfs.uns.Layout.UNRECOGNIZED : result;
  }

  public static final int OBJECT_TYPE_FIELD_NUMBER = 4;
  private volatile java.lang.Object objectType_;

  /**
   * <code>string object_type = 4;</code>
   *
   * @return The objectType.
   */
  public java.lang.String getObjectType() {
    java.lang.Object ref = objectType_;
    if (ref instanceof java.lang.String) {
      return (java.lang.String) ref;
    } else {
      com.google.protobuf.ByteString bs =
          (com.google.protobuf.ByteString) ref;
      java.lang.String s = bs.toStringUtf8();
      objectType_ = s;
      return s;
    }
  }

  /**
   * <code>string object_type = 4;</code>
   *
   * @return The bytes for objectType.
   */
  public com.google.protobuf.ByteString
      getObjectTypeBytes() {
    java.lang.Object ref = objectType_;
    if (ref instanceof java.lang.String) {
      com.google.protobuf.ByteString b =
          com.google.protobuf.ByteString.copyFromUtf8(
              (java.lang.String) ref);
      objectType_ = b;
      return b;
    } else {
      return (com.google.protobuf.ByteString) ref;
    }
  }

  public static final int CHUNK_SIZE_FIELD_NUMBER = 5;
  private long chunkSize_;

  /**
   * <code>uint64 chunk_size = 5;</code>
   *
   * @return The chunkSize.
   */
  public long getChunkSize() {
    return chunkSize_;
  }

  public static final int REL_PATH_FIELD_NUMBER = 6;
  private volatile java.lang.Object relPath_;

  /**
   * <code>string rel_path = 6;</code>
   *
   * @return The relPath.
   */
  public java.lang.String getRelPath() {
    java.lang.Object ref = relPath_;
    if (ref instanceof java.lang.String) {
      return (java.lang.String) ref;
    } else {
      com.google.protobuf.ByteString bs =
          (com.google.protobuf.ByteString) ref;
      java.lang.String s = bs.toStringUtf8();
      relPath_ = s;
      return s;
    }
  }

  /**
   * <code>string rel_path = 6;</code>
   *
   * @return The bytes for relPath.
   */
  public com.google.protobuf.ByteString
      getRelPathBytes() {
    java.lang.Object ref = relPath_;
    if (ref instanceof java.lang.String) {
      com.google.protobuf.ByteString b =
          com.google.protobuf.ByteString.copyFromUtf8(
              (java.lang.String) ref);
      relPath_ = b;
      return b;
    } else {
      return (com.google.protobuf.ByteString) ref;
    }
  }

  public static final int ON_LUSTRE_FIELD_NUMBER = 7;
  private boolean onLustre_;

  /**
   * <code>bool on_lustre = 7;</code>
   *
   * @return The onLustre.
   */
  public boolean getOnLustre() {
    return onLustre_;
  }

  public static final int PROPERTIES_FIELD_NUMBER = 8;
  private io.daos.dfs.uns.Properties properties_;

  /**
   * <code>.uns.Properties properties = 8;</code>
   *
   * @return Whether the properties field is set.
   */
  public boolean hasProperties() {
    return properties_ != null;
  }

  /**
   * <code>.uns.Properties properties = 8;</code>
   *
   * @return The properties.
   */
  public io.daos.dfs.uns.Properties getProperties() {
    return properties_ == null ? io.daos.dfs.uns.Properties.getDefaultInstance() : properties_;
  }

  /**
   * <code>.uns.Properties properties = 8;</code>
   */
  public io.daos.dfs.uns.PropertiesOrBuilder getPropertiesOrBuilder() {
    return getProperties();
  }

  public static final int NO_PREFIX_FIELD_NUMBER = 9;
  private boolean noPrefix_;

  /**
   * <code>bool no_prefix = 9;</code>
   *
   * @return The noPrefix.
   */
  public boolean getNoPrefix() {
    return noPrefix_;
  }

  private byte memoizedIsInitialized = -1;

  @java.lang.Override
  public final boolean isInitialized() {
    byte isInitialized = memoizedIsInitialized;
    if (isInitialized == 1) return true;
    if (isInitialized == 0) return false;

    memoizedIsInitialized = 1;
    return true;
  }

  @java.lang.Override
  public void writeTo(com.google.protobuf.CodedOutputStream output)
      throws java.io.IOException {
    if (!getPuuidBytes().isEmpty()) {
      com.google.protobuf.GeneratedMessageV3.writeString(output, 1, puuid_);
    }
    if (!getCuuidBytes().isEmpty()) {
      com.google.protobuf.GeneratedMessageV3.writeString(output, 2, cuuid_);
    }
    if (layoutType_ != io.daos.dfs.uns.Layout.UNKNOWN.getNumber()) {
      output.writeEnum(3, layoutType_);
    }
    if (!getObjectTypeBytes().isEmpty()) {
      com.google.protobuf.GeneratedMessageV3.writeString(output, 4, objectType_);
    }
    if (chunkSize_ != 0L) {
      output.writeUInt64(5, chunkSize_);
    }
    if (!getRelPathBytes().isEmpty()) {
      com.google.protobuf.GeneratedMessageV3.writeString(output, 6, relPath_);
    }
    if (onLustre_ != false) {
      output.writeBool(7, onLustre_);
    }
    if (properties_ != null) {
      output.writeMessage(8, getProperties());
    }
    if (noPrefix_ != false) {
      output.writeBool(9, noPrefix_);
    }
    unknownFields.writeTo(output);
  }

  @java.lang.Override
  public int getSerializedSize() {
    int size = memoizedSize;
    if (size != -1) return size;

    size = 0;
    if (!getPuuidBytes().isEmpty()) {
      size += com.google.protobuf.GeneratedMessageV3.computeStringSize(1, puuid_);
    }
    if (!getCuuidBytes().isEmpty()) {
      size += com.google.protobuf.GeneratedMessageV3.computeStringSize(2, cuuid_);
    }
    if (layoutType_ != io.daos.dfs.uns.Layout.UNKNOWN.getNumber()) {
      size += com.google.protobuf.CodedOutputStream
          .computeEnumSize(3, layoutType_);
    }
    if (!getObjectTypeBytes().isEmpty()) {
      size += com.google.protobuf.GeneratedMessageV3.computeStringSize(4, objectType_);
    }
    if (chunkSize_ != 0L) {
      size += com.google.protobuf.CodedOutputStream
          .computeUInt64Size(5, chunkSize_);
    }
    if (!getRelPathBytes().isEmpty()) {
      size += com.google.protobuf.GeneratedMessageV3.computeStringSize(6, relPath_);
    }
    if (onLustre_ != false) {
      size += com.google.protobuf.CodedOutputStream
          .computeBoolSize(7, onLustre_);
    }
    if (properties_ != null) {
      size += com.google.protobuf.CodedOutputStream
          .computeMessageSize(8, getProperties());
    }
    if (noPrefix_ != false) {
      size += com.google.protobuf.CodedOutputStream
          .computeBoolSize(9, noPrefix_);
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
    if (!(obj instanceof io.daos.dfs.uns.DunsAttribute)) {
      return super.equals(obj);
    }
    io.daos.dfs.uns.DunsAttribute other = (io.daos.dfs.uns.DunsAttribute) obj;

    if (!getPuuid()
        .equals(other.getPuuid())) return false;
    if (!getCuuid()
        .equals(other.getCuuid())) return false;
    if (layoutType_ != other.layoutType_) return false;
    if (!getObjectType()
        .equals(other.getObjectType())) return false;
    if (getChunkSize()
        != other.getChunkSize()) return false;
    if (!getRelPath()
        .equals(other.getRelPath())) return false;
    if (getOnLustre()
        != other.getOnLustre()) return false;
    if (hasProperties() != other.hasProperties()) return false;
    if (hasProperties()) {
      if (!getProperties()
          .equals(other.getProperties())) return false;
    }
    if (getNoPrefix()
        != other.getNoPrefix()) return false;
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
    hash = (37 * hash) + PUUID_FIELD_NUMBER;
    hash = (53 * hash) + getPuuid().hashCode();
    hash = (37 * hash) + CUUID_FIELD_NUMBER;
    hash = (53 * hash) + getCuuid().hashCode();
    hash = (37 * hash) + LAYOUT_TYPE_FIELD_NUMBER;
    hash = (53 * hash) + layoutType_;
    hash = (37 * hash) + OBJECT_TYPE_FIELD_NUMBER;
    hash = (53 * hash) + getObjectType().hashCode();
    hash = (37 * hash) + CHUNK_SIZE_FIELD_NUMBER;
    hash = (53 * hash) + com.google.protobuf.Internal.hashLong(
        getChunkSize());
    hash = (37 * hash) + REL_PATH_FIELD_NUMBER;
    hash = (53 * hash) + getRelPath().hashCode();
    hash = (37 * hash) + ON_LUSTRE_FIELD_NUMBER;
    hash = (53 * hash) + com.google.protobuf.Internal.hashBoolean(
        getOnLustre());
    if (hasProperties()) {
      hash = (37 * hash) + PROPERTIES_FIELD_NUMBER;
      hash = (53 * hash) + getProperties().hashCode();
    }
    hash = (37 * hash) + NO_PREFIX_FIELD_NUMBER;
    hash = (53 * hash) + com.google.protobuf.Internal.hashBoolean(
        getNoPrefix());
    hash = (29 * hash) + unknownFields.hashCode();
    memoizedHashCode = hash;
    return hash;
  }

  public static io.daos.dfs.uns.DunsAttribute parseFrom(
      java.nio.ByteBuffer data)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data);
  }

  public static io.daos.dfs.uns.DunsAttribute parseFrom(
      java.nio.ByteBuffer data,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data, extensionRegistry);
  }

  public static io.daos.dfs.uns.DunsAttribute parseFrom(
      com.google.protobuf.ByteString data)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data);
  }

  public static io.daos.dfs.uns.DunsAttribute parseFrom(
      com.google.protobuf.ByteString data,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data, extensionRegistry);
  }

  public static io.daos.dfs.uns.DunsAttribute parseFrom(byte[] data)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data);
  }

  public static io.daos.dfs.uns.DunsAttribute parseFrom(
      byte[] data,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data, extensionRegistry);
  }

  public static io.daos.dfs.uns.DunsAttribute parseFrom(java.io.InputStream input)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseWithIOException(PARSER, input);
  }

  public static io.daos.dfs.uns.DunsAttribute parseFrom(
      java.io.InputStream input,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseWithIOException(PARSER, input, extensionRegistry);
  }

  public static io.daos.dfs.uns.DunsAttribute parseDelimitedFrom(java.io.InputStream input)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseDelimitedWithIOException(PARSER, input);
  }

  public static io.daos.dfs.uns.DunsAttribute parseDelimitedFrom(
      java.io.InputStream input,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseDelimitedWithIOException(PARSER, input, extensionRegistry);
  }

  public static io.daos.dfs.uns.DunsAttribute parseFrom(
      com.google.protobuf.CodedInputStream input)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseWithIOException(PARSER, input);
  }

  public static io.daos.dfs.uns.DunsAttribute parseFrom(
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

  public static Builder newBuilder(io.daos.dfs.uns.DunsAttribute prototype) {
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
   * Protobuf type {@code uns.DunsAttribute}
   */
  public static final class Builder extends
      com.google.protobuf.GeneratedMessageV3.Builder<Builder> implements
      // @@protoc_insertion_point(builder_implements:uns.DunsAttribute)
      io.daos.dfs.uns.DunsAttributeOrBuilder {
    public static final com.google.protobuf.Descriptors.Descriptor
        getDescriptor() {
      return io.daos.dfs.uns.DunsClasses.internal_static_uns_DunsAttribute_descriptor;
    }

    @java.lang.Override
    protected com.google.protobuf.GeneratedMessageV3.FieldAccessorTable
        internalGetFieldAccessorTable() {
      return io.daos.dfs.uns.DunsClasses.internal_static_uns_DunsAttribute_fieldAccessorTable
          .ensureFieldAccessorsInitialized(
              io.daos.dfs.uns.DunsAttribute.class, io.daos.dfs.uns.DunsAttribute.Builder.class);
    }

    // Construct using io.daos.dfs.uns.DunsAttribute.newBuilder()
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
      puuid_ = "";

      cuuid_ = "";

      layoutType_ = 0;

      objectType_ = "";

      chunkSize_ = 0L;

      relPath_ = "";

      onLustre_ = false;

      if (propertiesBuilder_ == null) {
        properties_ = null;
      } else {
        properties_ = null;
        propertiesBuilder_ = null;
      }
      noPrefix_ = false;

      return this;
    }

    @java.lang.Override
    public com.google.protobuf.Descriptors.Descriptor
        getDescriptorForType() {
      return io.daos.dfs.uns.DunsClasses.internal_static_uns_DunsAttribute_descriptor;
    }

    @java.lang.Override
    public io.daos.dfs.uns.DunsAttribute getDefaultInstanceForType() {
      return io.daos.dfs.uns.DunsAttribute.getDefaultInstance();
    }

    @java.lang.Override
    public io.daos.dfs.uns.DunsAttribute build() {
      io.daos.dfs.uns.DunsAttribute result = buildPartial();
      if (!result.isInitialized()) {
        throw newUninitializedMessageException(result);
      }
      return result;
    }

    @java.lang.Override
    public io.daos.dfs.uns.DunsAttribute buildPartial() {
      io.daos.dfs.uns.DunsAttribute result = new io.daos.dfs.uns.DunsAttribute(this);
      result.puuid_ = puuid_;
      result.cuuid_ = cuuid_;
      result.layoutType_ = layoutType_;
      result.objectType_ = objectType_;
      result.chunkSize_ = chunkSize_;
      result.relPath_ = relPath_;
      result.onLustre_ = onLustre_;
      if (propertiesBuilder_ == null) {
        result.properties_ = properties_;
      } else {
        result.properties_ = propertiesBuilder_.build();
      }
      result.noPrefix_ = noPrefix_;
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
      if (other instanceof io.daos.dfs.uns.DunsAttribute) {
        return mergeFrom((io.daos.dfs.uns.DunsAttribute) other);
      } else {
        super.mergeFrom(other);
        return this;
      }
    }

    public Builder mergeFrom(io.daos.dfs.uns.DunsAttribute other) {
      if (other == io.daos.dfs.uns.DunsAttribute.getDefaultInstance()) return this;
      if (!other.getPuuid().isEmpty()) {
        puuid_ = other.puuid_;
        onChanged();
      }
      if (!other.getCuuid().isEmpty()) {
        cuuid_ = other.cuuid_;
        onChanged();
      }
      if (other.layoutType_ != 0) {
        setLayoutTypeValue(other.getLayoutTypeValue());
      }
      if (!other.getObjectType().isEmpty()) {
        objectType_ = other.objectType_;
        onChanged();
      }
      if (other.getChunkSize() != 0L) {
        setChunkSize(other.getChunkSize());
      }
      if (!other.getRelPath().isEmpty()) {
        relPath_ = other.relPath_;
        onChanged();
      }
      if (other.getOnLustre() != false) {
        setOnLustre(other.getOnLustre());
      }
      if (other.hasProperties()) {
        mergeProperties(other.getProperties());
      }
      if (other.getNoPrefix() != false) {
        setNoPrefix(other.getNoPrefix());
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
      io.daos.dfs.uns.DunsAttribute parsedMessage = null;
      try {
        parsedMessage = PARSER.parsePartialFrom(input, extensionRegistry);
      } catch (com.google.protobuf.InvalidProtocolBufferException e) {
        parsedMessage = (io.daos.dfs.uns.DunsAttribute) e.getUnfinishedMessage();
        throw e.unwrapIOException();
      } finally {
        if (parsedMessage != null) {
          mergeFrom(parsedMessage);
        }
      }
      return this;
    }

    private java.lang.Object puuid_ = "";

    /**
     * <code>string puuid = 1;</code>
     *
     * @return The puuid.
     */
    public java.lang.String getPuuid() {
      java.lang.Object ref = puuid_;
      if (!(ref instanceof java.lang.String)) {
        com.google.protobuf.ByteString bs =
            (com.google.protobuf.ByteString) ref;
        java.lang.String s = bs.toStringUtf8();
        puuid_ = s;
        return s;
      } else {
        return (java.lang.String) ref;
      }
    }

    /**
     * <code>string puuid = 1;</code>
     *
     * @return The bytes for puuid.
     */
    public com.google.protobuf.ByteString
        getPuuidBytes() {
      java.lang.Object ref = puuid_;
      if (ref instanceof String) {
        com.google.protobuf.ByteString b =
            com.google.protobuf.ByteString.copyFromUtf8(
                (java.lang.String) ref);
        puuid_ = b;
        return b;
      } else {
        return (com.google.protobuf.ByteString) ref;
      }
    }

    /**
     * <code>string puuid = 1;</code>
     *
     * @param value The puuid to set.
     * @return This builder for chaining.
     */
    public Builder setPuuid(
        java.lang.String value) {
      if (value == null) {
        throw new NullPointerException();
      }

      puuid_ = value;
      onChanged();
      return this;
    }

    /**
     * <code>string puuid = 1;</code>
     *
     * @return This builder for chaining.
     */
    public Builder clearPuuid() {

      puuid_ = getDefaultInstance().getPuuid();
      onChanged();
      return this;
    }

    /**
     * <code>string puuid = 1;</code>
     *
     * @param value The bytes for puuid to set.
     * @return This builder for chaining.
     */
    public Builder setPuuidBytes(
        com.google.protobuf.ByteString value) {
      if (value == null) {
        throw new NullPointerException();
      }
      checkByteStringIsUtf8(value);

      puuid_ = value;
      onChanged();
      return this;
    }

    private java.lang.Object cuuid_ = "";

    /**
     * <code>string cuuid = 2;</code>
     *
     * @return The cuuid.
     */
    public java.lang.String getCuuid() {
      java.lang.Object ref = cuuid_;
      if (!(ref instanceof java.lang.String)) {
        com.google.protobuf.ByteString bs =
            (com.google.protobuf.ByteString) ref;
        java.lang.String s = bs.toStringUtf8();
        cuuid_ = s;
        return s;
      } else {
        return (java.lang.String) ref;
      }
    }

    /**
     * <code>string cuuid = 2;</code>
     *
     * @return The bytes for cuuid.
     */
    public com.google.protobuf.ByteString
        getCuuidBytes() {
      java.lang.Object ref = cuuid_;
      if (ref instanceof String) {
        com.google.protobuf.ByteString b =
            com.google.protobuf.ByteString.copyFromUtf8(
                (java.lang.String) ref);
        cuuid_ = b;
        return b;
      } else {
        return (com.google.protobuf.ByteString) ref;
      }
    }

    /**
     * <code>string cuuid = 2;</code>
     *
     * @param value The cuuid to set.
     * @return This builder for chaining.
     */
    public Builder setCuuid(
        java.lang.String value) {
      if (value == null) {
        throw new NullPointerException();
      }

      cuuid_ = value;
      onChanged();
      return this;
    }

    /**
     * <code>string cuuid = 2;</code>
     *
     * @return This builder for chaining.
     */
    public Builder clearCuuid() {
      cuuid_ = getDefaultInstance().getCuuid();
      onChanged();
      return this;
    }

    /**
     * <code>string cuuid = 2;</code>
     *
     * @param value The bytes for cuuid to set.
     * @return This builder for chaining.
     */
    public Builder setCuuidBytes(
        com.google.protobuf.ByteString value) {
      if (value == null) {
        throw new NullPointerException();
      }
      checkByteStringIsUtf8(value);

      cuuid_ = value;
      onChanged();
      return this;
    }

    private int layoutType_ = 0;

    /**
     * <code>.uns.Layout layout_type = 3;</code>
     *
     * @return The enum numeric value on the wire for layoutType.
     */
    public int getLayoutTypeValue() {
      return layoutType_;
    }

    /**
     * <code>.uns.Layout layout_type = 3;</code>
     *
     * @param value The enum numeric value on the wire for layoutType to set.
     * @return This builder for chaining.
     */
    public Builder setLayoutTypeValue(int value) {
      layoutType_ = value;
      onChanged();
      return this;
    }

    /**
     * <code>.uns.Layout layout_type = 3;</code>
     *
     * @return The layoutType.
     */
    public io.daos.dfs.uns.Layout getLayoutType() {
      @SuppressWarnings("deprecation")
      io.daos.dfs.uns.Layout result = io.daos.dfs.uns.Layout.valueOf(layoutType_);
      return result == null ? io.daos.dfs.uns.Layout.UNRECOGNIZED : result;
    }

    /**
     * <code>.uns.Layout layout_type = 3;</code>
     *
     * @param value The layoutType to set.
     * @return This builder for chaining.
     */
    public Builder setLayoutType(io.daos.dfs.uns.Layout value) {
      if (value == null) {
        throw new NullPointerException();
      }

      layoutType_ = value.getNumber();
      onChanged();
      return this;
    }

    /**
     * <code>.uns.Layout layout_type = 3;</code>
     *
     * @return This builder for chaining.
     */
    public Builder clearLayoutType() {

      layoutType_ = 0;
      onChanged();
      return this;
    }

    private java.lang.Object objectType_ = "";

    /**
     * <code>string object_type = 4;</code>
     *
     * @return The objectType.
     */
    public java.lang.String getObjectType() {
      java.lang.Object ref = objectType_;
      if (!(ref instanceof java.lang.String)) {
        com.google.protobuf.ByteString bs =
            (com.google.protobuf.ByteString) ref;
        java.lang.String s = bs.toStringUtf8();
        objectType_ = s;
        return s;
      } else {
        return (java.lang.String) ref;
      }
    }

    /**
     * <code>string object_type = 4;</code>
     *
     * @return The bytes for objectType.
     */
    public com.google.protobuf.ByteString
        getObjectTypeBytes() {
      java.lang.Object ref = objectType_;
      if (ref instanceof String) {
        com.google.protobuf.ByteString b =
            com.google.protobuf.ByteString.copyFromUtf8(
                (java.lang.String) ref);
        objectType_ = b;
        return b;
      } else {
        return (com.google.protobuf.ByteString) ref;
      }
    }

    /**
     * <code>string object_type = 4;</code>
     *
     * @param value The objectType to set.
     * @return This builder for chaining.
     */
    public Builder setObjectType(
        java.lang.String value) {
      if (value == null) {
        throw new NullPointerException();
      }

      objectType_ = value;
      onChanged();
      return this;
    }

    /**
     * <code>string object_type = 4;</code>
     *
     * @return This builder for chaining.
     */
    public Builder clearObjectType() {

      objectType_ = getDefaultInstance().getObjectType();
      onChanged();
      return this;
    }

    /**
     * <code>string object_type = 4;</code>
     *
     * @param value The bytes for objectType to set.
     * @return This builder for chaining.
     */
    public Builder setObjectTypeBytes(
        com.google.protobuf.ByteString value) {
      if (value == null) {
        throw new NullPointerException();
      }
      checkByteStringIsUtf8(value);

      objectType_ = value;
      onChanged();
      return this;
    }

    private long chunkSize_;

    /**
     * <code>uint64 chunk_size = 5;</code>
     *
     * @return The chunkSize.
     */
    public long getChunkSize() {
      return chunkSize_;
    }

    /**
     * <code>uint64 chunk_size = 5;</code>
     *
     * @param value The chunkSize to set.
     * @return This builder for chaining.
     */
    public Builder setChunkSize(long value) {

      chunkSize_ = value;
      onChanged();
      return this;
    }

    /**
     * <code>uint64 chunk_size = 5;</code>
     *
     * @return This builder for chaining.
     */
    public Builder clearChunkSize() {

      chunkSize_ = 0L;
      onChanged();
      return this;
    }

    private java.lang.Object relPath_ = "";

    /**
     * <code>string rel_path = 6;</code>
     *
     * @return The relPath.
     */
    public java.lang.String getRelPath() {
      java.lang.Object ref = relPath_;
      if (!(ref instanceof java.lang.String)) {
        com.google.protobuf.ByteString bs =
            (com.google.protobuf.ByteString) ref;
        java.lang.String s = bs.toStringUtf8();
        relPath_ = s;
        return s;
      } else {
        return (java.lang.String) ref;
      }
    }

    /**
     * <code>string rel_path = 6;</code>
     *
     * @return The bytes for relPath.
     */
    public com.google.protobuf.ByteString
        getRelPathBytes() {
      java.lang.Object ref = relPath_;
      if (ref instanceof String) {
        com.google.protobuf.ByteString b =
            com.google.protobuf.ByteString.copyFromUtf8(
                (java.lang.String) ref);
        relPath_ = b;
        return b;
      } else {
        return (com.google.protobuf.ByteString) ref;
      }
    }

    /**
     * <code>string rel_path = 6;</code>
     *
     * @param value The relPath to set.
     * @return This builder for chaining.
     */
    public Builder setRelPath(
        java.lang.String value) {
      if (value == null) {
        throw new NullPointerException();
      }

      relPath_ = value;
      onChanged();
      return this;
    }

    /**
     * <code>string rel_path = 6;</code>
     *
     * @return This builder for chaining.
     */
    public Builder clearRelPath() {
      relPath_ = getDefaultInstance().getRelPath();
      onChanged();
      return this;
    }

    /**
     * <code>string rel_path = 6;</code>
     *
     * @param value The bytes for relPath to set.
     * @return This builder for chaining.
     */
    public Builder setRelPathBytes(
        com.google.protobuf.ByteString value) {
      if (value == null) {
        throw new NullPointerException();
      }
      checkByteStringIsUtf8(value);

      relPath_ = value;
      onChanged();
      return this;
    }

    private boolean onLustre_;

    /**
     * <code>bool on_lustre = 7;</code>
     *
     * @return The onLustre.
     */
    public boolean getOnLustre() {
      return onLustre_;
    }

    /**
     * <code>bool on_lustre = 7;</code>
     *
     * @param value The onLustre to set.
     * @return This builder for chaining.
     */
    public Builder setOnLustre(boolean value) {
      onLustre_ = value;
      onChanged();
      return this;
    }

    /**
     * <code>bool on_lustre = 7;</code>
     *
     * @return This builder for chaining.
     */
    public Builder clearOnLustre() {
      onLustre_ = false;
      onChanged();
      return this;
    }

    private io.daos.dfs.uns.Properties properties_;
    private com.google.protobuf.SingleFieldBuilderV3<
        io.daos.dfs.uns.Properties, io.daos.dfs.uns.Properties.Builder, io.daos.dfs.uns.PropertiesOrBuilder> propertiesBuilder_;

    /**
     * <code>.uns.Properties properties = 8;</code>
     *
     * @return Whether the properties field is set.
     */
    public boolean hasProperties() {
      return propertiesBuilder_ != null || properties_ != null;
    }

    /**
     * <code>.uns.Properties properties = 8;</code>
     *
     * @return The properties.
     */
    public io.daos.dfs.uns.Properties getProperties() {
      if (propertiesBuilder_ == null) {
        return properties_ == null ? io.daos.dfs.uns.Properties.getDefaultInstance() : properties_;
      } else {
        return propertiesBuilder_.getMessage();
      }
    }

    /**
     * <code>.uns.Properties properties = 8;</code>
     */
    public Builder setProperties(io.daos.dfs.uns.Properties value) {
      if (propertiesBuilder_ == null) {
        if (value == null) {
          throw new NullPointerException();
        }
        properties_ = value;
        onChanged();
      } else {
        propertiesBuilder_.setMessage(value);
      }

      return this;
    }

    /**
     * <code>.uns.Properties properties = 8;</code>
     */
    public Builder setProperties(
        io.daos.dfs.uns.Properties.Builder builderForValue) {
      if (propertiesBuilder_ == null) {
        properties_ = builderForValue.build();
        onChanged();
      } else {
        propertiesBuilder_.setMessage(builderForValue.build());
      }

      return this;
    }

    /**
     * <code>.uns.Properties properties = 8;</code>
     */
    public Builder mergeProperties(io.daos.dfs.uns.Properties value) {
      if (propertiesBuilder_ == null) {
        if (properties_ != null) {
          properties_ =
              io.daos.dfs.uns.Properties.newBuilder(properties_).mergeFrom(value).buildPartial();
        } else {
          properties_ = value;
        }
        onChanged();
      } else {
        propertiesBuilder_.mergeFrom(value);
      }

      return this;
    }

    /**
     * <code>.uns.Properties properties = 8;</code>
     */
    public Builder clearProperties() {
      if (propertiesBuilder_ == null) {
        properties_ = null;
        onChanged();
      } else {
        properties_ = null;
        propertiesBuilder_ = null;
      }

      return this;
    }

    /**
     * <code>.uns.Properties properties = 8;</code>
     */
    public io.daos.dfs.uns.Properties.Builder getPropertiesBuilder() {
      onChanged();
      return getPropertiesFieldBuilder().getBuilder();
    }

    /**
     * <code>.uns.Properties properties = 8;</code>
     */
    public io.daos.dfs.uns.PropertiesOrBuilder getPropertiesOrBuilder() {
      if (propertiesBuilder_ != null) {
        return propertiesBuilder_.getMessageOrBuilder();
      } else {
        return properties_ == null ?
            io.daos.dfs.uns.Properties.getDefaultInstance() : properties_;
      }
    }

    /**
     * <code>.uns.Properties properties = 8;</code>
     */
    private com.google.protobuf.SingleFieldBuilderV3<
        io.daos.dfs.uns.Properties, io.daos.dfs.uns.Properties.Builder, io.daos.dfs.uns.PropertiesOrBuilder>
        getPropertiesFieldBuilder() {
      if (propertiesBuilder_ == null) {
        propertiesBuilder_ = new com.google.protobuf.SingleFieldBuilderV3<
            io.daos.dfs.uns.Properties, io.daos.dfs.uns.Properties.Builder, io.daos.dfs.uns.PropertiesOrBuilder>(
            getProperties(),
            getParentForChildren(),
            isClean());
        properties_ = null;
      }
      return propertiesBuilder_;
    }

    private boolean noPrefix_;

    /**
     * <code>bool no_prefix = 9;</code>
     *
     * @return The noPrefix.
     */
    public boolean getNoPrefix() {
      return noPrefix_;
    }

    /**
     * <code>bool no_prefix = 9;</code>
     *
     * @param value The noPrefix to set.
     * @return This builder for chaining.
     */
    public Builder setNoPrefix(boolean value) {
      noPrefix_ = value;
      onChanged();
      return this;
    }

    /**
     * <code>bool no_prefix = 9;</code>
     *
     * @return This builder for chaining.
     */
    public Builder clearNoPrefix() {
      noPrefix_ = false;
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


    // @@protoc_insertion_point(builder_scope:uns.DunsAttribute)
  }

  // @@protoc_insertion_point(class_scope:uns.DunsAttribute)
  private static final io.daos.dfs.uns.DunsAttribute DEFAULT_INSTANCE;

  static {
    DEFAULT_INSTANCE = new io.daos.dfs.uns.DunsAttribute();
  }

  public static io.daos.dfs.uns.DunsAttribute getDefaultInstance() {
    return DEFAULT_INSTANCE;
  }

  private static final com.google.protobuf.Parser<DunsAttribute>
      PARSER = new com.google.protobuf.AbstractParser<DunsAttribute>() {
    @java.lang.Override
    public DunsAttribute parsePartialFrom(
        com.google.protobuf.CodedInputStream input,
        com.google.protobuf.ExtensionRegistryLite extensionRegistry)
        throws com.google.protobuf.InvalidProtocolBufferException {
      return new DunsAttribute(input, extensionRegistry);
    }
  };

  public static com.google.protobuf.Parser<DunsAttribute> parser() {
    return PARSER;
  }

  @java.lang.Override
  public com.google.protobuf.Parser<DunsAttribute> getParserForType() {
    return PARSER;
  }

  @java.lang.Override
  public io.daos.dfs.uns.DunsAttribute getDefaultInstanceForType() {
    return DEFAULT_INSTANCE;
  }

}
