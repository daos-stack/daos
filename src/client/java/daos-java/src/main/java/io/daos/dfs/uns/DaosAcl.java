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
 * Protobuf type {@code uns.DaosAcl}
 */
public final class DaosAcl extends
    com.google.protobuf.GeneratedMessageV3 implements
    // @@protoc_insertion_point(message_implements:uns.DaosAcl)
    DaosAclOrBuilder {
  private static final long serialVersionUID = 0L;

  // Use DaosAcl.newBuilder() to construct.
  private DaosAcl(com.google.protobuf.GeneratedMessageV3.Builder<?> builder) {
    super(builder);
  }

  private DaosAcl() {
    aces_ = java.util.Collections.emptyList();
  }

  @java.lang.Override
  @SuppressWarnings({"unused"})
  protected java.lang.Object newInstance(
      UnusedPrivateParameter unused) {
    return new DaosAcl();
  }

  @java.lang.Override
  public final com.google.protobuf.UnknownFieldSet
  getUnknownFields() {
    return this.unknownFields;
  }

  private DaosAcl(
      com.google.protobuf.CodedInputStream input,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws com.google.protobuf.InvalidProtocolBufferException {
    this();
    if (extensionRegistry == null) {
      throw new java.lang.NullPointerException();
    }
    int mutable_bitField0_ = 0;
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

            ver_ = input.readUInt32();
            break;
          }
          case 16: {

            reserv_ = input.readUInt32();
            break;
          }
          case 34: {
            if (!((mutable_bitField0_ & 0x00000001) != 0)) {
              aces_ = new java.util.ArrayList<io.daos.dfs.uns.DaosAce>();
              mutable_bitField0_ |= 0x00000001;
            }
            aces_.add(
                input.readMessage(io.daos.dfs.uns.DaosAce.parser(), extensionRegistry));
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
      if (((mutable_bitField0_ & 0x00000001) != 0)) {
        aces_ = java.util.Collections.unmodifiableList(aces_);
      }
      this.unknownFields = unknownFields.build();
      makeExtensionsImmutable();
    }
  }

  public static final com.google.protobuf.Descriptors.Descriptor
  getDescriptor() {
    return io.daos.dfs.uns.DunsClasses.internal_static_uns_DaosAcl_descriptor;
  }

  @java.lang.Override
  protected com.google.protobuf.GeneratedMessageV3.FieldAccessorTable
  internalGetFieldAccessorTable() {
    return io.daos.dfs.uns.DunsClasses.internal_static_uns_DaosAcl_fieldAccessorTable
        .ensureFieldAccessorsInitialized(
            io.daos.dfs.uns.DaosAcl.class, io.daos.dfs.uns.DaosAcl.Builder.class);
  }

  public static final int VER_FIELD_NUMBER = 1;
  private int ver_;

  /**
   * <code>uint32 ver = 1;</code>
   *
   * @return The ver.
   */
  public int getVer() {
    return ver_;
  }

  public static final int RESERV_FIELD_NUMBER = 2;
  private int reserv_;

  /**
   * <code>uint32 reserv = 2;</code>
   *
   * @return The reserv.
   */
  public int getReserv() {
    return reserv_;
  }

  public static final int ACES_FIELD_NUMBER = 4;
  private java.util.List<io.daos.dfs.uns.DaosAce> aces_;

  /**
   * <code>repeated .uns.DaosAce aces = 4;</code>
   */
  public java.util.List<io.daos.dfs.uns.DaosAce> getAcesList() {
    return aces_;
  }

  /**
   * <code>repeated .uns.DaosAce aces = 4;</code>
   */
  public java.util.List<? extends io.daos.dfs.uns.DaosAceOrBuilder>
  getAcesOrBuilderList() {
    return aces_;
  }

  /**
   * <code>repeated .uns.DaosAce aces = 4;</code>
   */
  public int getAcesCount() {
    return aces_.size();
  }

  /**
   * <code>repeated .uns.DaosAce aces = 4;</code>
   */
  public io.daos.dfs.uns.DaosAce getAces(int index) {
    return aces_.get(index);
  }

  /**
   * <code>repeated .uns.DaosAce aces = 4;</code>
   */
  public io.daos.dfs.uns.DaosAceOrBuilder getAcesOrBuilder(
      int index) {
    return aces_.get(index);
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
    if (ver_ != 0) {
      output.writeUInt32(1, ver_);
    }
    if (reserv_ != 0) {
      output.writeUInt32(2, reserv_);
    }
    for (int i = 0; i < aces_.size(); i++) {
      output.writeMessage(4, aces_.get(i));
    }
    unknownFields.writeTo(output);
  }

  @java.lang.Override
  public int getSerializedSize() {
    int size = memoizedSize;
    if (size != -1) return size;

    size = 0;
    if (ver_ != 0) {
      size += com.google.protobuf.CodedOutputStream
          .computeUInt32Size(1, ver_);
    }
    if (reserv_ != 0) {
      size += com.google.protobuf.CodedOutputStream
          .computeUInt32Size(2, reserv_);
    }
    for (int i = 0; i < aces_.size(); i++) {
      size += com.google.protobuf.CodedOutputStream
          .computeMessageSize(4, aces_.get(i));
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
    if (!(obj instanceof io.daos.dfs.uns.DaosAcl)) {
      return super.equals(obj);
    }
    io.daos.dfs.uns.DaosAcl other = (io.daos.dfs.uns.DaosAcl) obj;

    if (getVer()
        != other.getVer()) return false;
    if (getReserv()
        != other.getReserv()) return false;
    if (!getAcesList()
        .equals(other.getAcesList())) return false;
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
    hash = (37 * hash) + VER_FIELD_NUMBER;
    hash = (53 * hash) + getVer();
    hash = (37 * hash) + RESERV_FIELD_NUMBER;
    hash = (53 * hash) + getReserv();
    if (getAcesCount() > 0) {
      hash = (37 * hash) + ACES_FIELD_NUMBER;
      hash = (53 * hash) + getAcesList().hashCode();
    }
    hash = (29 * hash) + unknownFields.hashCode();
    memoizedHashCode = hash;
    return hash;
  }

  public static io.daos.dfs.uns.DaosAcl parseFrom(
      java.nio.ByteBuffer data)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data);
  }

  public static io.daos.dfs.uns.DaosAcl parseFrom(
      java.nio.ByteBuffer data,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data, extensionRegistry);
  }

  public static io.daos.dfs.uns.DaosAcl parseFrom(
      com.google.protobuf.ByteString data)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data);
  }

  public static io.daos.dfs.uns.DaosAcl parseFrom(
      com.google.protobuf.ByteString data,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data, extensionRegistry);
  }

  public static io.daos.dfs.uns.DaosAcl parseFrom(byte[] data)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data);
  }

  public static io.daos.dfs.uns.DaosAcl parseFrom(
      byte[] data,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data, extensionRegistry);
  }

  public static io.daos.dfs.uns.DaosAcl parseFrom(java.io.InputStream input)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseWithIOException(PARSER, input);
  }

  public static io.daos.dfs.uns.DaosAcl parseFrom(
      java.io.InputStream input,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseWithIOException(PARSER, input, extensionRegistry);
  }

  public static io.daos.dfs.uns.DaosAcl parseDelimitedFrom(java.io.InputStream input)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseDelimitedWithIOException(PARSER, input);
  }

  public static io.daos.dfs.uns.DaosAcl parseDelimitedFrom(
      java.io.InputStream input,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseDelimitedWithIOException(PARSER, input, extensionRegistry);
  }

  public static io.daos.dfs.uns.DaosAcl parseFrom(
      com.google.protobuf.CodedInputStream input)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseWithIOException(PARSER, input);
  }

  public static io.daos.dfs.uns.DaosAcl parseFrom(
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

  public static Builder newBuilder(io.daos.dfs.uns.DaosAcl prototype) {
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
   * Protobuf type {@code uns.DaosAcl}
   */
  public static final class Builder extends
      com.google.protobuf.GeneratedMessageV3.Builder<Builder> implements
      // @@protoc_insertion_point(builder_implements:uns.DaosAcl)
      io.daos.dfs.uns.DaosAclOrBuilder {
    public static final com.google.protobuf.Descriptors.Descriptor
    getDescriptor() {
      return io.daos.dfs.uns.DunsClasses.internal_static_uns_DaosAcl_descriptor;
    }

    @java.lang.Override
    protected com.google.protobuf.GeneratedMessageV3.FieldAccessorTable
    internalGetFieldAccessorTable() {
      return io.daos.dfs.uns.DunsClasses.internal_static_uns_DaosAcl_fieldAccessorTable
          .ensureFieldAccessorsInitialized(
              io.daos.dfs.uns.DaosAcl.class, io.daos.dfs.uns.DaosAcl.Builder.class);
    }

    // Construct using io.daos.dfs.uns.DaosAcl.newBuilder()
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
        getAcesFieldBuilder();
      }
    }

    @java.lang.Override
    public Builder clear() {
      super.clear();
      ver_ = 0;

      reserv_ = 0;

      if (acesBuilder_ == null) {
        aces_ = java.util.Collections.emptyList();
        bitField0_ = (bitField0_ & ~0x00000001);
      } else {
        acesBuilder_.clear();
      }
      return this;
    }

    @java.lang.Override
    public com.google.protobuf.Descriptors.Descriptor
    getDescriptorForType() {
      return io.daos.dfs.uns.DunsClasses.internal_static_uns_DaosAcl_descriptor;
    }

    @java.lang.Override
    public io.daos.dfs.uns.DaosAcl getDefaultInstanceForType() {
      return io.daos.dfs.uns.DaosAcl.getDefaultInstance();
    }

    @java.lang.Override
    public io.daos.dfs.uns.DaosAcl build() {
      io.daos.dfs.uns.DaosAcl result = buildPartial();
      if (!result.isInitialized()) {
        throw newUninitializedMessageException(result);
      }
      return result;
    }

    @java.lang.Override
    public io.daos.dfs.uns.DaosAcl buildPartial() {
      io.daos.dfs.uns.DaosAcl result = new io.daos.dfs.uns.DaosAcl(this);
      int from_bitField0_ = bitField0_;
      result.ver_ = ver_;
      result.reserv_ = reserv_;
      if (acesBuilder_ == null) {
        if (((bitField0_ & 0x00000001) != 0)) {
          aces_ = java.util.Collections.unmodifiableList(aces_);
          bitField0_ = (bitField0_ & ~0x00000001);
        }
        result.aces_ = aces_;
      } else {
        result.aces_ = acesBuilder_.build();
      }
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
      if (other instanceof io.daos.dfs.uns.DaosAcl) {
        return mergeFrom((io.daos.dfs.uns.DaosAcl) other);
      } else {
        super.mergeFrom(other);
        return this;
      }
    }

    public Builder mergeFrom(io.daos.dfs.uns.DaosAcl other) {
      if (other == io.daos.dfs.uns.DaosAcl.getDefaultInstance()) return this;
      if (other.getVer() != 0) {
        setVer(other.getVer());
      }
      if (other.getReserv() != 0) {
        setReserv(other.getReserv());
      }
      if (acesBuilder_ == null) {
        if (!other.aces_.isEmpty()) {
          if (aces_.isEmpty()) {
            aces_ = other.aces_;
            bitField0_ = (bitField0_ & ~0x00000001);
          } else {
            ensureAcesIsMutable();
            aces_.addAll(other.aces_);
          }
          onChanged();
        }
      } else {
        if (!other.aces_.isEmpty()) {
          if (acesBuilder_.isEmpty()) {
            acesBuilder_.dispose();
            acesBuilder_ = null;
            aces_ = other.aces_;
            bitField0_ = (bitField0_ & ~0x00000001);
            acesBuilder_ =
                com.google.protobuf.GeneratedMessageV3.alwaysUseFieldBuilders ?
                    getAcesFieldBuilder() : null;
          } else {
            acesBuilder_.addAllMessages(other.aces_);
          }
        }
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
      io.daos.dfs.uns.DaosAcl parsedMessage = null;
      try {
        parsedMessage = PARSER.parsePartialFrom(input, extensionRegistry);
      } catch (com.google.protobuf.InvalidProtocolBufferException e) {
        parsedMessage = (io.daos.dfs.uns.DaosAcl) e.getUnfinishedMessage();
        throw e.unwrapIOException();
      } finally {
        if (parsedMessage != null) {
          mergeFrom(parsedMessage);
        }
      }
      return this;
    }

    private int bitField0_;

    private int ver_;

    /**
     * <code>uint32 ver = 1;</code>
     *
     * @return The ver.
     */
    public int getVer() {
      return ver_;
    }

    /**
     * <code>uint32 ver = 1;</code>
     *
     * @param value The ver to set.
     * @return This builder for chaining.
     */
    public Builder setVer(int value) {

      ver_ = value;
      onChanged();
      return this;
    }

    /**
     * <code>uint32 ver = 1;</code>
     *
     * @return This builder for chaining.
     */
    public Builder clearVer() {

      ver_ = 0;
      onChanged();
      return this;
    }

    private int reserv_;

    /**
     * <code>uint32 reserv = 2;</code>
     *
     * @return The reserv.
     */
    public int getReserv() {
      return reserv_;
    }

    /**
     * <code>uint32 reserv = 2;</code>
     *
     * @param value The reserv to set.
     * @return This builder for chaining.
     */
    public Builder setReserv(int value) {

      reserv_ = value;
      onChanged();
      return this;
    }

    /**
     * <code>uint32 reserv = 2;</code>
     *
     * @return This builder for chaining.
     */
    public Builder clearReserv() {

      reserv_ = 0;
      onChanged();
      return this;
    }

    private java.util.List<io.daos.dfs.uns.DaosAce> aces_ =
        java.util.Collections.emptyList();

    private void ensureAcesIsMutable() {
      if (!((bitField0_ & 0x00000001) != 0)) {
        aces_ = new java.util.ArrayList<io.daos.dfs.uns.DaosAce>(aces_);
        bitField0_ |= 0x00000001;
      }
    }

    private com.google.protobuf.RepeatedFieldBuilderV3<
        io.daos.dfs.uns.DaosAce, io.daos.dfs.uns.DaosAce.Builder, io.daos.dfs.uns.DaosAceOrBuilder> acesBuilder_;

    /**
     * <code>repeated .uns.DaosAce aces = 4;</code>
     */
    public java.util.List<io.daos.dfs.uns.DaosAce> getAcesList() {
      if (acesBuilder_ == null) {
        return java.util.Collections.unmodifiableList(aces_);
      } else {
        return acesBuilder_.getMessageList();
      }
    }

    /**
     * <code>repeated .uns.DaosAce aces = 4;</code>
     */
    public int getAcesCount() {
      if (acesBuilder_ == null) {
        return aces_.size();
      } else {
        return acesBuilder_.getCount();
      }
    }

    /**
     * <code>repeated .uns.DaosAce aces = 4;</code>
     */
    public io.daos.dfs.uns.DaosAce getAces(int index) {
      if (acesBuilder_ == null) {
        return aces_.get(index);
      } else {
        return acesBuilder_.getMessage(index);
      }
    }

    /**
     * <code>repeated .uns.DaosAce aces = 4;</code>
     */
    public Builder setAces(
        int index, io.daos.dfs.uns.DaosAce value) {
      if (acesBuilder_ == null) {
        if (value == null) {
          throw new NullPointerException();
        }
        ensureAcesIsMutable();
        aces_.set(index, value);
        onChanged();
      } else {
        acesBuilder_.setMessage(index, value);
      }
      return this;
    }

    /**
     * <code>repeated .uns.DaosAce aces = 4;</code>
     */
    public Builder setAces(
        int index, io.daos.dfs.uns.DaosAce.Builder builderForValue) {
      if (acesBuilder_ == null) {
        ensureAcesIsMutable();
        aces_.set(index, builderForValue.build());
        onChanged();
      } else {
        acesBuilder_.setMessage(index, builderForValue.build());
      }
      return this;
    }

    /**
     * <code>repeated .uns.DaosAce aces = 4;</code>
     */
    public Builder addAces(io.daos.dfs.uns.DaosAce value) {
      if (acesBuilder_ == null) {
        if (value == null) {
          throw new NullPointerException();
        }
        ensureAcesIsMutable();
        aces_.add(value);
        onChanged();
      } else {
        acesBuilder_.addMessage(value);
      }
      return this;
    }

    /**
     * <code>repeated .uns.DaosAce aces = 4;</code>
     */
    public Builder addAces(
        int index, io.daos.dfs.uns.DaosAce value) {
      if (acesBuilder_ == null) {
        if (value == null) {
          throw new NullPointerException();
        }
        ensureAcesIsMutable();
        aces_.add(index, value);
        onChanged();
      } else {
        acesBuilder_.addMessage(index, value);
      }
      return this;
    }

    /**
     * <code>repeated .uns.DaosAce aces = 4;</code>
     */
    public Builder addAces(
        io.daos.dfs.uns.DaosAce.Builder builderForValue) {
      if (acesBuilder_ == null) {
        ensureAcesIsMutable();
        aces_.add(builderForValue.build());
        onChanged();
      } else {
        acesBuilder_.addMessage(builderForValue.build());
      }
      return this;
    }

    /**
     * <code>repeated .uns.DaosAce aces = 4;</code>
     */
    public Builder addAces(
        int index, io.daos.dfs.uns.DaosAce.Builder builderForValue) {
      if (acesBuilder_ == null) {
        ensureAcesIsMutable();
        aces_.add(index, builderForValue.build());
        onChanged();
      } else {
        acesBuilder_.addMessage(index, builderForValue.build());
      }
      return this;
    }

    /**
     * <code>repeated .uns.DaosAce aces = 4;</code>
     */
    public Builder addAllAces(
        java.lang.Iterable<? extends io.daos.dfs.uns.DaosAce> values) {
      if (acesBuilder_ == null) {
        ensureAcesIsMutable();
        com.google.protobuf.AbstractMessageLite.Builder.addAll(
            values, aces_);
        onChanged();
      } else {
        acesBuilder_.addAllMessages(values);
      }
      return this;
    }

    /**
     * <code>repeated .uns.DaosAce aces = 4;</code>
     */
    public Builder clearAces() {
      if (acesBuilder_ == null) {
        aces_ = java.util.Collections.emptyList();
        bitField0_ = (bitField0_ & ~0x00000001);
        onChanged();
      } else {
        acesBuilder_.clear();
      }
      return this;
    }

    /**
     * <code>repeated .uns.DaosAce aces = 4;</code>
     */
    public Builder removeAces(int index) {
      if (acesBuilder_ == null) {
        ensureAcesIsMutable();
        aces_.remove(index);
        onChanged();
      } else {
        acesBuilder_.remove(index);
      }
      return this;
    }

    /**
     * <code>repeated .uns.DaosAce aces = 4;</code>
     */
    public io.daos.dfs.uns.DaosAce.Builder getAcesBuilder(
        int index) {
      return getAcesFieldBuilder().getBuilder(index);
    }

    /**
     * <code>repeated .uns.DaosAce aces = 4;</code>
     */
    public io.daos.dfs.uns.DaosAceOrBuilder getAcesOrBuilder(
        int index) {
      if (acesBuilder_ == null) {
        return aces_.get(index);
      } else {
        return acesBuilder_.getMessageOrBuilder(index);
      }
    }

    /**
     * <code>repeated .uns.DaosAce aces = 4;</code>
     */
    public java.util.List<? extends io.daos.dfs.uns.DaosAceOrBuilder>
        getAcesOrBuilderList() {
      if (acesBuilder_ != null) {
        return acesBuilder_.getMessageOrBuilderList();
      } else {
        return java.util.Collections.unmodifiableList(aces_);
      }
    }

    /**
     * <code>repeated .uns.DaosAce aces = 4;</code>
     */
    public io.daos.dfs.uns.DaosAce.Builder addAcesBuilder() {
      return getAcesFieldBuilder().addBuilder(
          io.daos.dfs.uns.DaosAce.getDefaultInstance());
    }

    /**
     * <code>repeated .uns.DaosAce aces = 4;</code>
     */
    public io.daos.dfs.uns.DaosAce.Builder addAcesBuilder(
        int index) {
      return getAcesFieldBuilder().addBuilder(
          index, io.daos.dfs.uns.DaosAce.getDefaultInstance());
    }

    /**
     * <code>repeated .uns.DaosAce aces = 4;</code>
     */
    public java.util.List<io.daos.dfs.uns.DaosAce.Builder>
    getAcesBuilderList() {
      return getAcesFieldBuilder().getBuilderList();
    }

    private com.google.protobuf.RepeatedFieldBuilderV3<
        io.daos.dfs.uns.DaosAce, io.daos.dfs.uns.DaosAce.Builder, io.daos.dfs.uns.DaosAceOrBuilder>
        getAcesFieldBuilder() {
      if (acesBuilder_ == null) {
        acesBuilder_ = new com.google.protobuf.RepeatedFieldBuilderV3<
            io.daos.dfs.uns.DaosAce, io.daos.dfs.uns.DaosAce.Builder, io.daos.dfs.uns.DaosAceOrBuilder>(
            aces_,
            ((bitField0_ & 0x00000001) != 0),
            getParentForChildren(),
            isClean());
        aces_ = null;
      }
      return acesBuilder_;
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


    // @@protoc_insertion_point(builder_scope:uns.DaosAcl)
  }

  // @@protoc_insertion_point(class_scope:uns.DaosAcl)
  private static final io.daos.dfs.uns.DaosAcl DEFAULT_INSTANCE;

  static {
    DEFAULT_INSTANCE = new io.daos.dfs.uns.DaosAcl();
  }

  public static io.daos.dfs.uns.DaosAcl getDefaultInstance() {
    return DEFAULT_INSTANCE;
  }

  private static final com.google.protobuf.Parser<DaosAcl>
      PARSER = new com.google.protobuf.AbstractParser<DaosAcl>() {
    @java.lang.Override
    public DaosAcl parsePartialFrom(
        com.google.protobuf.CodedInputStream input,
        com.google.protobuf.ExtensionRegistryLite extensionRegistry)
        throws com.google.protobuf.InvalidProtocolBufferException {
      return new DaosAcl(input, extensionRegistry);
    }
  };

  public static com.google.protobuf.Parser<DaosAcl> parser() {
    return PARSER;
  }

  @java.lang.Override
  public com.google.protobuf.Parser<DaosAcl> getParserForType() {
    return PARSER;
  }

  @java.lang.Override
  public io.daos.dfs.uns.DaosAcl getDefaultInstanceForType() {
    return DEFAULT_INSTANCE;
  }

}
