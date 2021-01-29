/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.dfs.uns;

/**
 * Protobuf type {@code uns.Properties}
 */
public final class Properties extends
    com.google.protobuf.GeneratedMessageV3 implements
    // @@protoc_insertion_point(message_implements:uns.Properties)
    PropertiesOrBuilder {
  private static final long serialVersionUID = 0L;

  // Use Properties.newBuilder() to construct.
  private Properties(com.google.protobuf.GeneratedMessageV3.Builder<?> builder) {
    super(builder);
  }

  private Properties() {
    entries_ = java.util.Collections.emptyList();
  }

  @java.lang.Override
  @SuppressWarnings({"unused"})
  protected java.lang.Object newInstance(
      UnusedPrivateParameter unused) {
    return new Properties();
  }

  @java.lang.Override
  public final com.google.protobuf.UnknownFieldSet
      getUnknownFields() {
    return this.unknownFields;
  }

  private Properties(
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

            reserved_ = input.readUInt32();
            break;
          }
          case 18: {
            if (!((mutable_bitField0_ & 0x00000001) != 0)) {
              entries_ = new java.util.ArrayList<io.daos.dfs.uns.Entry>();
              mutable_bitField0_ |= 0x00000001;
            }
            entries_.add(
                input.readMessage(io.daos.dfs.uns.Entry.parser(), extensionRegistry));
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
        entries_ = java.util.Collections.unmodifiableList(entries_);
      }
      this.unknownFields = unknownFields.build();
      makeExtensionsImmutable();
    }
  }

  public static final com.google.protobuf.Descriptors.Descriptor
      getDescriptor() {
    return io.daos.dfs.uns.DunsClasses.internal_static_uns_Properties_descriptor;
  }

  @java.lang.Override
  protected com.google.protobuf.GeneratedMessageV3.FieldAccessorTable
      internalGetFieldAccessorTable() {
    return io.daos.dfs.uns.DunsClasses.internal_static_uns_Properties_fieldAccessorTable
        .ensureFieldAccessorsInitialized(
            io.daos.dfs.uns.Properties.class, io.daos.dfs.uns.Properties.Builder.class);
  }

  public static final int RESERVED_FIELD_NUMBER = 1;
  private int reserved_;

  /**
   * <code>uint32 reserved = 1;</code>
   *
   * @return The reserved.
   */
  public int getReserved() {
    return reserved_;
  }

  public static final int ENTRIES_FIELD_NUMBER = 2;
  private java.util.List<io.daos.dfs.uns.Entry> entries_;

  /**
   * <code>repeated .uns.Entry entries = 2;</code>
   */
  public java.util.List<io.daos.dfs.uns.Entry> getEntriesList() {
    return entries_;
  }

  /**
   * <code>repeated .uns.Entry entries = 2;</code>
   */
  public java.util.List<? extends io.daos.dfs.uns.EntryOrBuilder>
      getEntriesOrBuilderList() {
    return entries_;
  }

  /**
   * <code>repeated .uns.Entry entries = 2;</code>
   */
  public int getEntriesCount() {
    return entries_.size();
  }

  /**
   * <code>repeated .uns.Entry entries = 2;</code>
   */
  public io.daos.dfs.uns.Entry getEntries(int index) {
    return entries_.get(index);
  }

  /**
   * <code>repeated .uns.Entry entries = 2;</code>
   */
  public io.daos.dfs.uns.EntryOrBuilder getEntriesOrBuilder(
      int index) {
    return entries_.get(index);
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
    if (reserved_ != 0) {
      output.writeUInt32(1, reserved_);
    }
    for (int i = 0; i < entries_.size(); i++) {
      output.writeMessage(2, entries_.get(i));
    }
    unknownFields.writeTo(output);
  }

  @java.lang.Override
  public int getSerializedSize() {
    int size = memoizedSize;
    if (size != -1) return size;

    size = 0;
    if (reserved_ != 0) {
      size += com.google.protobuf.CodedOutputStream
          .computeUInt32Size(1, reserved_);
    }
    for (int i = 0; i < entries_.size(); i++) {
      size += com.google.protobuf.CodedOutputStream
          .computeMessageSize(2, entries_.get(i));
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
    if (!(obj instanceof io.daos.dfs.uns.Properties)) {
      return super.equals(obj);
    }
    io.daos.dfs.uns.Properties other = (io.daos.dfs.uns.Properties) obj;

    if (getReserved()
        != other.getReserved()) return false;
    if (!getEntriesList()
        .equals(other.getEntriesList())) return false;
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
    hash = (37 * hash) + RESERVED_FIELD_NUMBER;
    hash = (53 * hash) + getReserved();
    if (getEntriesCount() > 0) {
      hash = (37 * hash) + ENTRIES_FIELD_NUMBER;
      hash = (53 * hash) + getEntriesList().hashCode();
    }
    hash = (29 * hash) + unknownFields.hashCode();
    memoizedHashCode = hash;
    return hash;
  }

  public static io.daos.dfs.uns.Properties parseFrom(
      java.nio.ByteBuffer data)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data);
  }

  public static io.daos.dfs.uns.Properties parseFrom(
      java.nio.ByteBuffer data,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data, extensionRegistry);
  }

  public static io.daos.dfs.uns.Properties parseFrom(
      com.google.protobuf.ByteString data)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data);
  }

  public static io.daos.dfs.uns.Properties parseFrom(
      com.google.protobuf.ByteString data,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data, extensionRegistry);
  }

  public static io.daos.dfs.uns.Properties parseFrom(byte[] data)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data);
  }

  public static io.daos.dfs.uns.Properties parseFrom(
      byte[] data,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data, extensionRegistry);
  }

  public static io.daos.dfs.uns.Properties parseFrom(java.io.InputStream input)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseWithIOException(PARSER, input);
  }

  public static io.daos.dfs.uns.Properties parseFrom(
      java.io.InputStream input,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseWithIOException(PARSER, input, extensionRegistry);
  }

  public static io.daos.dfs.uns.Properties parseDelimitedFrom(java.io.InputStream input)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseDelimitedWithIOException(PARSER, input);
  }

  public static io.daos.dfs.uns.Properties parseDelimitedFrom(
      java.io.InputStream input,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseDelimitedWithIOException(PARSER, input, extensionRegistry);
  }

  public static io.daos.dfs.uns.Properties parseFrom(
      com.google.protobuf.CodedInputStream input)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseWithIOException(PARSER, input);
  }

  public static io.daos.dfs.uns.Properties parseFrom(
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

  public static Builder newBuilder(io.daos.dfs.uns.Properties prototype) {
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
   * Protobuf type {@code uns.Properties}
   */
  public static final class Builder extends
      com.google.protobuf.GeneratedMessageV3.Builder<Builder> implements
      // @@protoc_insertion_point(builder_implements:uns.Properties)
      io.daos.dfs.uns.PropertiesOrBuilder {
    public static final com.google.protobuf.Descriptors.Descriptor
        getDescriptor() {
      return io.daos.dfs.uns.DunsClasses.internal_static_uns_Properties_descriptor;
    }

    @java.lang.Override
    protected com.google.protobuf.GeneratedMessageV3.FieldAccessorTable
        internalGetFieldAccessorTable() {
      return io.daos.dfs.uns.DunsClasses.internal_static_uns_Properties_fieldAccessorTable
          .ensureFieldAccessorsInitialized(
              io.daos.dfs.uns.Properties.class, io.daos.dfs.uns.Properties.Builder.class);
    }

    // Construct using io.daos.dfs.uns.Properties.newBuilder()
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
        getEntriesFieldBuilder();
      }
    }

    @java.lang.Override
    public Builder clear() {
      super.clear();
      reserved_ = 0;

      if (entriesBuilder_ == null) {
        entries_ = java.util.Collections.emptyList();
        bitField0_ = (bitField0_ & ~0x00000001);
      } else {
        entriesBuilder_.clear();
      }
      return this;
    }

    @java.lang.Override
    public com.google.protobuf.Descriptors.Descriptor
        getDescriptorForType() {
      return io.daos.dfs.uns.DunsClasses.internal_static_uns_Properties_descriptor;
    }

    @java.lang.Override
    public io.daos.dfs.uns.Properties getDefaultInstanceForType() {
      return io.daos.dfs.uns.Properties.getDefaultInstance();
    }

    @java.lang.Override
    public io.daos.dfs.uns.Properties build() {
      io.daos.dfs.uns.Properties result = buildPartial();
      if (!result.isInitialized()) {
        throw newUninitializedMessageException(result);
      }
      return result;
    }

    @java.lang.Override
    public io.daos.dfs.uns.Properties buildPartial() {
      io.daos.dfs.uns.Properties result = new io.daos.dfs.uns.Properties(this);
      int from_bitField0_ = bitField0_;
      result.reserved_ = reserved_;
      if (entriesBuilder_ == null) {
        if (((bitField0_ & 0x00000001) != 0)) {
          entries_ = java.util.Collections.unmodifiableList(entries_);
          bitField0_ = (bitField0_ & ~0x00000001);
        }
        result.entries_ = entries_;
      } else {
        result.entries_ = entriesBuilder_.build();
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
      if (other instanceof io.daos.dfs.uns.Properties) {
        return mergeFrom((io.daos.dfs.uns.Properties) other);
      } else {
        super.mergeFrom(other);
        return this;
      }
    }

    public Builder mergeFrom(io.daos.dfs.uns.Properties other) {
      if (other == io.daos.dfs.uns.Properties.getDefaultInstance()) return this;
      if (other.getReserved() != 0) {
        setReserved(other.getReserved());
      }
      if (entriesBuilder_ == null) {
        if (!other.entries_.isEmpty()) {
          if (entries_.isEmpty()) {
            entries_ = other.entries_;
            bitField0_ = (bitField0_ & ~0x00000001);
          } else {
            ensureEntriesIsMutable();
            entries_.addAll(other.entries_);
          }
          onChanged();
        }
      } else {
        if (!other.entries_.isEmpty()) {
          if (entriesBuilder_.isEmpty()) {
            entriesBuilder_.dispose();
            entriesBuilder_ = null;
            entries_ = other.entries_;
            bitField0_ = (bitField0_ & ~0x00000001);
            entriesBuilder_ =
                com.google.protobuf.GeneratedMessageV3.alwaysUseFieldBuilders ?
                    getEntriesFieldBuilder() : null;
          } else {
            entriesBuilder_.addAllMessages(other.entries_);
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
      io.daos.dfs.uns.Properties parsedMessage = null;
      try {
        parsedMessage = PARSER.parsePartialFrom(input, extensionRegistry);
      } catch (com.google.protobuf.InvalidProtocolBufferException e) {
        parsedMessage = (io.daos.dfs.uns.Properties) e.getUnfinishedMessage();
        throw e.unwrapIOException();
      } finally {
        if (parsedMessage != null) {
          mergeFrom(parsedMessage);
        }
      }
      return this;
    }

    private int bitField0_;

    private int reserved_;

    /**
     * <code>uint32 reserved = 1;</code>
     *
     * @return The reserved.
     */
    public int getReserved() {
      return reserved_;
    }

    /**
     * <code>uint32 reserved = 1;</code>
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
     * <code>uint32 reserved = 1;</code>
     *
     * @return This builder for chaining.
     */
    public Builder clearReserved() {

      reserved_ = 0;
      onChanged();
      return this;
    }

    private java.util.List<io.daos.dfs.uns.Entry> entries_ =
        java.util.Collections.emptyList();

    private void ensureEntriesIsMutable() {
      if (!((bitField0_ & 0x00000001) != 0)) {
        entries_ = new java.util.ArrayList<io.daos.dfs.uns.Entry>(entries_);
        bitField0_ |= 0x00000001;
      }
    }

    private com.google.protobuf.RepeatedFieldBuilderV3<
        io.daos.dfs.uns.Entry, io.daos.dfs.uns.Entry.Builder, io.daos.dfs.uns.EntryOrBuilder> entriesBuilder_;

    /**
     * <code>repeated .uns.Entry entries = 2;</code>
     */
    public java.util.List<io.daos.dfs.uns.Entry> getEntriesList() {
      if (entriesBuilder_ == null) {
        return java.util.Collections.unmodifiableList(entries_);
      } else {
        return entriesBuilder_.getMessageList();
      }
    }

    /**
     * <code>repeated .uns.Entry entries = 2;</code>
     */
    public int getEntriesCount() {
      if (entriesBuilder_ == null) {
        return entries_.size();
      } else {
        return entriesBuilder_.getCount();
      }
    }

    /**
     * <code>repeated .uns.Entry entries = 2;</code>
     */
    public io.daos.dfs.uns.Entry getEntries(int index) {
      if (entriesBuilder_ == null) {
        return entries_.get(index);
      } else {
        return entriesBuilder_.getMessage(index);
      }
    }

    /**
     * <code>repeated .uns.Entry entries = 2;</code>
     */
    public Builder setEntries(
        int index, io.daos.dfs.uns.Entry value) {
      if (entriesBuilder_ == null) {
        if (value == null) {
          throw new NullPointerException();
        }
        ensureEntriesIsMutable();
        entries_.set(index, value);
        onChanged();
      } else {
        entriesBuilder_.setMessage(index, value);
      }
      return this;
    }

    /**
     * <code>repeated .uns.Entry entries = 2;</code>
     */
    public Builder setEntries(
        int index, io.daos.dfs.uns.Entry.Builder builderForValue) {
      if (entriesBuilder_ == null) {
        ensureEntriesIsMutable();
        entries_.set(index, builderForValue.build());
        onChanged();
      } else {
        entriesBuilder_.setMessage(index, builderForValue.build());
      }
      return this;
    }

    /**
     * <code>repeated .uns.Entry entries = 2;</code>
     */
    public Builder addEntries(io.daos.dfs.uns.Entry value) {
      if (entriesBuilder_ == null) {
        if (value == null) {
          throw new NullPointerException();
        }
        ensureEntriesIsMutable();
        entries_.add(value);
        onChanged();
      } else {
        entriesBuilder_.addMessage(value);
      }
      return this;
    }

    /**
     * <code>repeated .uns.Entry entries = 2;</code>
     */
    public Builder addEntries(
        int index, io.daos.dfs.uns.Entry value) {
      if (entriesBuilder_ == null) {
        if (value == null) {
          throw new NullPointerException();
        }
        ensureEntriesIsMutable();
        entries_.add(index, value);
        onChanged();
      } else {
        entriesBuilder_.addMessage(index, value);
      }
      return this;
    }

    /**
     * <code>repeated .uns.Entry entries = 2;</code>
     */
    public Builder addEntries(
        io.daos.dfs.uns.Entry.Builder builderForValue) {
      if (entriesBuilder_ == null) {
        ensureEntriesIsMutable();
        entries_.add(builderForValue.build());
        onChanged();
      } else {
        entriesBuilder_.addMessage(builderForValue.build());
      }
      return this;
    }

    /**
     * <code>repeated .uns.Entry entries = 2;</code>
     */
    public Builder addEntries(
        int index, io.daos.dfs.uns.Entry.Builder builderForValue) {
      if (entriesBuilder_ == null) {
        ensureEntriesIsMutable();
        entries_.add(index, builderForValue.build());
        onChanged();
      } else {
        entriesBuilder_.addMessage(index, builderForValue.build());
      }
      return this;
    }

    /**
     * <code>repeated .uns.Entry entries = 2;</code>
     */
    public Builder addAllEntries(
        java.lang.Iterable<? extends io.daos.dfs.uns.Entry> values) {
      if (entriesBuilder_ == null) {
        ensureEntriesIsMutable();
        com.google.protobuf.AbstractMessageLite.Builder.addAll(
            values, entries_);
        onChanged();
      } else {
        entriesBuilder_.addAllMessages(values);
      }
      return this;
    }

    /**
     * <code>repeated .uns.Entry entries = 2;</code>
     */
    public Builder clearEntries() {
      if (entriesBuilder_ == null) {
        entries_ = java.util.Collections.emptyList();
        bitField0_ = (bitField0_ & ~0x00000001);
        onChanged();
      } else {
        entriesBuilder_.clear();
      }
      return this;
    }

    /**
     * <code>repeated .uns.Entry entries = 2;</code>
     */
    public Builder removeEntries(int index) {
      if (entriesBuilder_ == null) {
        ensureEntriesIsMutable();
        entries_.remove(index);
        onChanged();
      } else {
        entriesBuilder_.remove(index);
      }
      return this;
    }

    /**
     * <code>repeated .uns.Entry entries = 2;</code>
     */
    public io.daos.dfs.uns.Entry.Builder getEntriesBuilder(
        int index) {
      return getEntriesFieldBuilder().getBuilder(index);
    }

    /**
     * <code>repeated .uns.Entry entries = 2;</code>
     */
    public io.daos.dfs.uns.EntryOrBuilder getEntriesOrBuilder(
        int index) {
      if (entriesBuilder_ == null) {
        return entries_.get(index);
      } else {
        return entriesBuilder_.getMessageOrBuilder(index);
      }
    }

    /**
     * <code>repeated .uns.Entry entries = 2;</code>
     */
    public java.util.List<? extends io.daos.dfs.uns.EntryOrBuilder>
        getEntriesOrBuilderList() {
      if (entriesBuilder_ != null) {
        return entriesBuilder_.getMessageOrBuilderList();
      } else {
        return java.util.Collections.unmodifiableList(entries_);
      }
    }

    /**
     * <code>repeated .uns.Entry entries = 2;</code>
     */
    public io.daos.dfs.uns.Entry.Builder addEntriesBuilder() {
      return getEntriesFieldBuilder().addBuilder(
          io.daos.dfs.uns.Entry.getDefaultInstance());
    }

    /**
     * <code>repeated .uns.Entry entries = 2;</code>
     */
    public io.daos.dfs.uns.Entry.Builder addEntriesBuilder(
        int index) {
      return getEntriesFieldBuilder().addBuilder(
          index, io.daos.dfs.uns.Entry.getDefaultInstance());
    }

    /**
     * <code>repeated .uns.Entry entries = 2;</code>
     */
    public java.util.List<io.daos.dfs.uns.Entry.Builder>
        getEntriesBuilderList() {
      return getEntriesFieldBuilder().getBuilderList();
    }

    private com.google.protobuf.RepeatedFieldBuilderV3<
        io.daos.dfs.uns.Entry, io.daos.dfs.uns.Entry.Builder, io.daos.dfs.uns.EntryOrBuilder>
        getEntriesFieldBuilder() {
      if (entriesBuilder_ == null) {
        entriesBuilder_ = new com.google.protobuf.RepeatedFieldBuilderV3<
            io.daos.dfs.uns.Entry, io.daos.dfs.uns.Entry.Builder, io.daos.dfs.uns.EntryOrBuilder>(
            entries_,
            ((bitField0_ & 0x00000001) != 0),
            getParentForChildren(),
            isClean());
        entries_ = null;
      }
      return entriesBuilder_;
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


    // @@protoc_insertion_point(builder_scope:uns.Properties)
  }

  // @@protoc_insertion_point(class_scope:uns.Properties)
  private static final io.daos.dfs.uns.Properties DEFAULT_INSTANCE;

  static {
    DEFAULT_INSTANCE = new io.daos.dfs.uns.Properties();
  }

  public static io.daos.dfs.uns.Properties getDefaultInstance() {
    return DEFAULT_INSTANCE;
  }

  private static final com.google.protobuf.Parser<Properties>
      PARSER = new com.google.protobuf.AbstractParser<Properties>() {
    @java.lang.Override
    public Properties parsePartialFrom(
        com.google.protobuf.CodedInputStream input,
        com.google.protobuf.ExtensionRegistryLite extensionRegistry)
        throws com.google.protobuf.InvalidProtocolBufferException {
      return new Properties(input, extensionRegistry);
    }
  };

  public static com.google.protobuf.Parser<Properties> parser() {
    return PARSER;
  }

  @java.lang.Override
  public com.google.protobuf.Parser<Properties> getParserForType() {
    return PARSER;
  }

  @java.lang.Override
  public io.daos.dfs.uns.Properties getDefaultInstanceForType() {
    return DEFAULT_INSTANCE;
  }

}
