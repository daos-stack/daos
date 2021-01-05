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
 * Protobuf type {@code uns.Entry}
 */
public final class Entry extends
    com.google.protobuf.GeneratedMessageV3 implements
    // @@protoc_insertion_point(message_implements:uns.Entry)
    EntryOrBuilder {
  private static final long serialVersionUID = 0L;

  // Use Entry.newBuilder() to construct.
  private Entry(com.google.protobuf.GeneratedMessageV3.Builder<?> builder) {
    super(builder);
  }

  private Entry() {
    type_ = 0;
  }

  @java.lang.Override
  @SuppressWarnings({"unused"})
  protected java.lang.Object newInstance(
      UnusedPrivateParameter unused) {
    return new Entry();
  }

  @java.lang.Override
  public final com.google.protobuf.UnknownFieldSet
      getUnknownFields() {
    return this.unknownFields;
  }

  private Entry(
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
            int rawValue = input.readEnum();

            type_ = rawValue;
            break;
          }
          case 16: {

            reserved_ = input.readUInt32();
            break;
          }
          case 24: {
            valueCase_ = 3;
            value_ = input.readUInt64();
            break;
          }
          case 34: {
            java.lang.String s = input.readStringRequireUtf8();
            valueCase_ = 4;
            value_ = s;
            break;
          }
          case 42: {
            io.daos.dfs.uns.DaosAcl.Builder subBuilder = null;
            if (valueCase_ == 5) {
              subBuilder = ((io.daos.dfs.uns.DaosAcl) value_).toBuilder();
            }
            value_ =
                input.readMessage(io.daos.dfs.uns.DaosAcl.parser(), extensionRegistry);
            if (subBuilder != null) {
              subBuilder.mergeFrom((io.daos.dfs.uns.DaosAcl) value_);
              value_ = subBuilder.buildPartial();
            }
            valueCase_ = 5;
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
    return io.daos.dfs.uns.DunsClasses.internal_static_uns_Entry_descriptor;
  }

  @java.lang.Override
  protected com.google.protobuf.GeneratedMessageV3.FieldAccessorTable
      internalGetFieldAccessorTable() {
    return io.daos.dfs.uns.DunsClasses.internal_static_uns_Entry_fieldAccessorTable
        .ensureFieldAccessorsInitialized(
            io.daos.dfs.uns.Entry.class, io.daos.dfs.uns.Entry.Builder.class);
  }

  private int valueCase_ = 0;
  private java.lang.Object value_;

  public enum ValueCase
      implements com.google.protobuf.Internal.EnumLite,
      com.google.protobuf.AbstractMessage.InternalOneOfEnum {
    VAL(3),
    STR(4),
    PVAL(5),
    VALUE_NOT_SET(0);
    private final int value;

    private ValueCase(int value) {
      this.value = value;
    }

    /**
     * @param value The number of the enum to look for.
     * @return The enum associated with the given number.
     * @deprecated Use {@link #forNumber(int)} instead.
     */
    @java.lang.Deprecated
    public static ValueCase valueOf(int value) {
      return forNumber(value);
    }

    public static ValueCase forNumber(int value) {
      switch (value) {
        case 3:
          return VAL;
        case 4:
          return STR;
        case 5:
          return PVAL;
        case 0:
          return VALUE_NOT_SET;
        default:
          return null;
      }
    }

    public int getNumber() {
      return this.value;
    }
  }

  ;

  public ValueCase
      getValueCase() {
    return ValueCase.forNumber(
        valueCase_);
  }

  public static final int TYPE_FIELD_NUMBER = 1;
  private int type_;

  /**
   * <code>.uns.PropType type = 1;</code>
   *
   * @return The enum numeric value on the wire for type.
   */
  public int getTypeValue() {
    return type_;
  }

  /**
   * <code>.uns.PropType type = 1;</code>
   *
   * @return The type.
   */
  public io.daos.dfs.uns.PropType getType() {
    @SuppressWarnings("deprecation")
    io.daos.dfs.uns.PropType result = io.daos.dfs.uns.PropType.valueOf(type_);
    return result == null ? io.daos.dfs.uns.PropType.UNRECOGNIZED : result;
  }

  public static final int RESERVED_FIELD_NUMBER = 2;
  private int reserved_;

  /**
   * <code>uint32 reserved = 2;</code>
   *
   * @return The reserved.
   */
  public int getReserved() {
    return reserved_;
  }

  public static final int VAL_FIELD_NUMBER = 3;

  /**
   * <code>uint64 val = 3;</code>
   *
   * @return The val.
   */
  public long getVal() {
    if (valueCase_ == 3) {
      return (java.lang.Long) value_;
    }
    return 0L;
  }

  public static final int STR_FIELD_NUMBER = 4;

  /**
   * <code>string str = 4;</code>
   *
   * @return The str.
   */
  public java.lang.String getStr() {
    java.lang.Object ref = "";
    if (valueCase_ == 4) {
      ref = value_;
    }
    if (ref instanceof java.lang.String) {
      return (java.lang.String) ref;
    } else {
      com.google.protobuf.ByteString bs =
          (com.google.protobuf.ByteString) ref;
      java.lang.String s = bs.toStringUtf8();
      if (valueCase_ == 4) {
        value_ = s;
      }
      return s;
    }
  }

  /**
   * <code>string str = 4;</code>
   *
   * @return The bytes for str.
   */
  public com.google.protobuf.ByteString
      getStrBytes() {
    java.lang.Object ref = "";
    if (valueCase_ == 4) {
      ref = value_;
    }
    if (ref instanceof java.lang.String) {
      com.google.protobuf.ByteString b =
          com.google.protobuf.ByteString.copyFromUtf8(
              (java.lang.String) ref);
      if (valueCase_ == 4) {
        value_ = b;
      }
      return b;
    } else {
      return (com.google.protobuf.ByteString) ref;
    }
  }

  public static final int PVAL_FIELD_NUMBER = 5;

  /**
   * <code>.uns.DaosAcl pval = 5;</code>
   *
   * @return Whether the pval field is set.
   */
  public boolean hasPval() {
    return valueCase_ == 5;
  }

  /**
   * <code>.uns.DaosAcl pval = 5;</code>
   *
   * @return The pval.
   */
  public io.daos.dfs.uns.DaosAcl getPval() {
    if (valueCase_ == 5) {
      return (io.daos.dfs.uns.DaosAcl) value_;
    }
    return io.daos.dfs.uns.DaosAcl.getDefaultInstance();
  }

  /**
   * <code>.uns.DaosAcl pval = 5;</code>
   */
  public io.daos.dfs.uns.DaosAclOrBuilder getPvalOrBuilder() {
    if (valueCase_ == 5) {
      return (io.daos.dfs.uns.DaosAcl) value_;
    }
    return io.daos.dfs.uns.DaosAcl.getDefaultInstance();
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
    if (type_ != io.daos.dfs.uns.PropType.DAOS_PROP_PO_MIN.getNumber()) {
      output.writeEnum(1, type_);
    }
    if (reserved_ != 0) {
      output.writeUInt32(2, reserved_);
    }
    if (valueCase_ == 3) {
      output.writeUInt64(
          3, (long) ((java.lang.Long) value_));
    }
    if (valueCase_ == 4) {
      com.google.protobuf.GeneratedMessageV3.writeString(output, 4, value_);
    }
    if (valueCase_ == 5) {
      output.writeMessage(5, (io.daos.dfs.uns.DaosAcl) value_);
    }
    unknownFields.writeTo(output);
  }

  @java.lang.Override
  public int getSerializedSize() {
    int size = memoizedSize;
    if (size != -1) return size;

    size = 0;
    if (type_ != io.daos.dfs.uns.PropType.DAOS_PROP_PO_MIN.getNumber()) {
      size += com.google.protobuf.CodedOutputStream
          .computeEnumSize(1, type_);
    }
    if (reserved_ != 0) {
      size += com.google.protobuf.CodedOutputStream
          .computeUInt32Size(2, reserved_);
    }
    if (valueCase_ == 3) {
      size += com.google.protobuf.CodedOutputStream
          .computeUInt64Size(
              3, (long) ((java.lang.Long) value_));
    }
    if (valueCase_ == 4) {
      size += com.google.protobuf.GeneratedMessageV3.computeStringSize(4, value_);
    }
    if (valueCase_ == 5) {
      size += com.google.protobuf.CodedOutputStream
          .computeMessageSize(5, (io.daos.dfs.uns.DaosAcl) value_);
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
    if (!(obj instanceof io.daos.dfs.uns.Entry)) {
      return super.equals(obj);
    }
    io.daos.dfs.uns.Entry other = (io.daos.dfs.uns.Entry) obj;

    if (type_ != other.type_) return false;
    if (getReserved()
        != other.getReserved()) return false;
    if (!getValueCase().equals(other.getValueCase())) return false;
    switch (valueCase_) {
      case 3:
        if (getVal()
            != other.getVal()) return false;
        break;
      case 4:
        if (!getStr()
            .equals(other.getStr())) return false;
        break;
      case 5:
        if (!getPval()
            .equals(other.getPval())) return false;
        break;
      case 0:
      default:
    }
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
    hash = (37 * hash) + TYPE_FIELD_NUMBER;
    hash = (53 * hash) + type_;
    hash = (37 * hash) + RESERVED_FIELD_NUMBER;
    hash = (53 * hash) + getReserved();
    switch (valueCase_) {
      case 3:
        hash = (37 * hash) + VAL_FIELD_NUMBER;
        hash = (53 * hash) + com.google.protobuf.Internal.hashLong(
            getVal());
        break;
      case 4:
        hash = (37 * hash) + STR_FIELD_NUMBER;
        hash = (53 * hash) + getStr().hashCode();
        break;
      case 5:
        hash = (37 * hash) + PVAL_FIELD_NUMBER;
        hash = (53 * hash) + getPval().hashCode();
        break;
      case 0:
      default:
    }
    hash = (29 * hash) + unknownFields.hashCode();
    memoizedHashCode = hash;
    return hash;
  }

  public static io.daos.dfs.uns.Entry parseFrom(
      java.nio.ByteBuffer data)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data);
  }

  public static io.daos.dfs.uns.Entry parseFrom(
      java.nio.ByteBuffer data,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data, extensionRegistry);
  }

  public static io.daos.dfs.uns.Entry parseFrom(
      com.google.protobuf.ByteString data)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data);
  }

  public static io.daos.dfs.uns.Entry parseFrom(
      com.google.protobuf.ByteString data,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data, extensionRegistry);
  }

  public static io.daos.dfs.uns.Entry parseFrom(byte[] data)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data);
  }

  public static io.daos.dfs.uns.Entry parseFrom(
      byte[] data,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data, extensionRegistry);
  }

  public static io.daos.dfs.uns.Entry parseFrom(java.io.InputStream input)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseWithIOException(PARSER, input);
  }

  public static io.daos.dfs.uns.Entry parseFrom(
      java.io.InputStream input,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseWithIOException(PARSER, input, extensionRegistry);
  }

  public static io.daos.dfs.uns.Entry parseDelimitedFrom(java.io.InputStream input)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseDelimitedWithIOException(PARSER, input);
  }

  public static io.daos.dfs.uns.Entry parseDelimitedFrom(
      java.io.InputStream input,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseDelimitedWithIOException(PARSER, input, extensionRegistry);
  }

  public static io.daos.dfs.uns.Entry parseFrom(
      com.google.protobuf.CodedInputStream input)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseWithIOException(PARSER, input);
  }

  public static io.daos.dfs.uns.Entry parseFrom(
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

  public static Builder newBuilder(io.daos.dfs.uns.Entry prototype) {
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
   * Protobuf type {@code uns.Entry}
   */
  public static final class Builder extends
      com.google.protobuf.GeneratedMessageV3.Builder<Builder> implements
      // @@protoc_insertion_point(builder_implements:uns.Entry)
      io.daos.dfs.uns.EntryOrBuilder {
    public static final com.google.protobuf.Descriptors.Descriptor
        getDescriptor() {
      return io.daos.dfs.uns.DunsClasses.internal_static_uns_Entry_descriptor;
    }

    @java.lang.Override
    protected com.google.protobuf.GeneratedMessageV3.FieldAccessorTable
        internalGetFieldAccessorTable() {
      return io.daos.dfs.uns.DunsClasses.internal_static_uns_Entry_fieldAccessorTable
          .ensureFieldAccessorsInitialized(
              io.daos.dfs.uns.Entry.class, io.daos.dfs.uns.Entry.Builder.class);
    }

    // Construct using io.daos.dfs.uns.Entry.newBuilder()
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
      type_ = 0;

      reserved_ = 0;

      valueCase_ = 0;
      value_ = null;
      return this;
    }

    @java.lang.Override
    public com.google.protobuf.Descriptors.Descriptor
        getDescriptorForType() {
      return io.daos.dfs.uns.DunsClasses.internal_static_uns_Entry_descriptor;
    }

    @java.lang.Override
    public io.daos.dfs.uns.Entry getDefaultInstanceForType() {
      return io.daos.dfs.uns.Entry.getDefaultInstance();
    }

    @java.lang.Override
    public io.daos.dfs.uns.Entry build() {
      io.daos.dfs.uns.Entry result = buildPartial();
      if (!result.isInitialized()) {
        throw newUninitializedMessageException(result);
      }
      return result;
    }

    @java.lang.Override
    public io.daos.dfs.uns.Entry buildPartial() {
      io.daos.dfs.uns.Entry result = new io.daos.dfs.uns.Entry(this);
      result.type_ = type_;
      result.reserved_ = reserved_;
      if (valueCase_ == 3) {
        result.value_ = value_;
      }
      if (valueCase_ == 4) {
        result.value_ = value_;
      }
      if (valueCase_ == 5) {
        if (pvalBuilder_ == null) {
          result.value_ = value_;
        } else {
          result.value_ = pvalBuilder_.build();
        }
      }
      result.valueCase_ = valueCase_;
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
      if (other instanceof io.daos.dfs.uns.Entry) {
        return mergeFrom((io.daos.dfs.uns.Entry) other);
      } else {
        super.mergeFrom(other);
        return this;
      }
    }

    public Builder mergeFrom(io.daos.dfs.uns.Entry other) {
      if (other == io.daos.dfs.uns.Entry.getDefaultInstance()) return this;
      if (other.type_ != 0) {
        setTypeValue(other.getTypeValue());
      }
      if (other.getReserved() != 0) {
        setReserved(other.getReserved());
      }
      switch (other.getValueCase()) {
        case VAL: {
          setVal(other.getVal());
          break;
        }
        case STR: {
          valueCase_ = 4;
          value_ = other.value_;
          onChanged();
          break;
        }
        case PVAL: {
          mergePval(other.getPval());
          break;
        }
        case VALUE_NOT_SET: {
          break;
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
      io.daos.dfs.uns.Entry parsedMessage = null;
      try {
        parsedMessage = PARSER.parsePartialFrom(input, extensionRegistry);
      } catch (com.google.protobuf.InvalidProtocolBufferException e) {
        parsedMessage = (io.daos.dfs.uns.Entry) e.getUnfinishedMessage();
        throw e.unwrapIOException();
      } finally {
        if (parsedMessage != null) {
          mergeFrom(parsedMessage);
        }
      }
      return this;
    }

    private int valueCase_ = 0;
    private java.lang.Object value_;

    public ValueCase
        getValueCase() {
      return ValueCase.forNumber(
          valueCase_);
    }

    public Builder clearValue() {
      valueCase_ = 0;
      value_ = null;
      onChanged();
      return this;
    }


    private int type_ = 0;

    /**
     * <code>.uns.PropType type = 1;</code>
     *
     * @return The enum numeric value on the wire for type.
     */
    public int getTypeValue() {
      return type_;
    }

    /**
     * <code>.uns.PropType type = 1;</code>
     *
     * @param value The enum numeric value on the wire for type to set.
     * @return This builder for chaining.
     */
    public Builder setTypeValue(int value) {
      type_ = value;
      onChanged();
      return this;
    }

    /**
     * <code>.uns.PropType type = 1;</code>
     *
     * @return The type.
     */
    public io.daos.dfs.uns.PropType getType() {
      @SuppressWarnings("deprecation")
      io.daos.dfs.uns.PropType result = io.daos.dfs.uns.PropType.valueOf(type_);
      return result == null ? io.daos.dfs.uns.PropType.UNRECOGNIZED : result;
    }

    /**
     * <code>.uns.PropType type = 1;</code>
     *
     * @param value The type to set.
     * @return This builder for chaining.
     */
    public Builder setType(io.daos.dfs.uns.PropType value) {
      if (value == null) {
        throw new NullPointerException();
      }

      type_ = value.getNumber();
      onChanged();
      return this;
    }

    /**
     * <code>.uns.PropType type = 1;</code>
     *
     * @return This builder for chaining.
     */
    public Builder clearType() {
      type_ = 0;
      onChanged();
      return this;
    }

    private int reserved_;

    /**
     * <code>uint32 reserved = 2;</code>
     *
     * @return The reserved.
     */
    public int getReserved() {
      return reserved_;
    }

    /**
     * <code>uint32 reserved = 2;</code>
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
     * <code>uint32 reserved = 2;</code>
     *
     * @return This builder for chaining.
     */
    public Builder clearReserved() {
      reserved_ = 0;
      onChanged();
      return this;
    }

    /**
     * <code>uint64 val = 3;</code>
     *
     * @return The val.
     */
    public long getVal() {
      if (valueCase_ == 3) {
        return (java.lang.Long) value_;
      }
      return 0L;
    }

    /**
     * <code>uint64 val = 3;</code>
     *
     * @param value The val to set.
     * @return This builder for chaining.
     */
    public Builder setVal(long value) {
      valueCase_ = 3;
      value_ = value;
      onChanged();
      return this;
    }

    /**
     * <code>uint64 val = 3;</code>
     *
     * @return This builder for chaining.
     */
    public Builder clearVal() {
      if (valueCase_ == 3) {
        valueCase_ = 0;
        value_ = null;
        onChanged();
      }
      return this;
    }

    /**
     * <code>string str = 4;</code>
     *
     * @return The str.
     */
    public java.lang.String getStr() {
      java.lang.Object ref = "";
      if (valueCase_ == 4) {
        ref = value_;
      }
      if (!(ref instanceof java.lang.String)) {
        com.google.protobuf.ByteString bs =
            (com.google.protobuf.ByteString) ref;
        java.lang.String s = bs.toStringUtf8();
        if (valueCase_ == 4) {
          value_ = s;
        }
        return s;
      } else {
        return (java.lang.String) ref;
      }
    }

    /**
     * <code>string str = 4;</code>
     *
     * @return The bytes for str.
     */
    public com.google.protobuf.ByteString
        getStrBytes() {
      java.lang.Object ref = "";
      if (valueCase_ == 4) {
        ref = value_;
      }
      if (ref instanceof String) {
        com.google.protobuf.ByteString b =
            com.google.protobuf.ByteString.copyFromUtf8(
                (java.lang.String) ref);
        if (valueCase_ == 4) {
          value_ = b;
        }
        return b;
      } else {
        return (com.google.protobuf.ByteString) ref;
      }
    }

    /**
     * <code>string str = 4;</code>
     *
     * @param value The str to set.
     * @return This builder for chaining.
     */
    public Builder setStr(
        java.lang.String value) {
      if (value == null) {
        throw new NullPointerException();
      }
      valueCase_ = 4;
      value_ = value;
      onChanged();
      return this;
    }

    /**
     * <code>string str = 4;</code>
     *
     * @return This builder for chaining.
     */
    public Builder clearStr() {
      if (valueCase_ == 4) {
        valueCase_ = 0;
        value_ = null;
        onChanged();
      }
      return this;
    }

    /**
     * <code>string str = 4;</code>
     *
     * @param value The bytes for str to set.
     * @return This builder for chaining.
     */
    public Builder setStrBytes(
        com.google.protobuf.ByteString value) {
      if (value == null) {
        throw new NullPointerException();
      }
      checkByteStringIsUtf8(value);
      valueCase_ = 4;
      value_ = value;
      onChanged();
      return this;
    }

    private com.google.protobuf.SingleFieldBuilderV3<
        io.daos.dfs.uns.DaosAcl, io.daos.dfs.uns.DaosAcl.Builder, io.daos.dfs.uns.DaosAclOrBuilder> pvalBuilder_;

    /**
     * <code>.uns.DaosAcl pval = 5;</code>
     *
     * @return Whether the pval field is set.
     */
    public boolean hasPval() {
      return valueCase_ == 5;
    }

    /**
     * <code>.uns.DaosAcl pval = 5;</code>
     *
     * @return The pval.
     */
    public io.daos.dfs.uns.DaosAcl getPval() {
      if (pvalBuilder_ == null) {
        if (valueCase_ == 5) {
          return (io.daos.dfs.uns.DaosAcl) value_;
        }
        return io.daos.dfs.uns.DaosAcl.getDefaultInstance();
      } else {
        if (valueCase_ == 5) {
          return pvalBuilder_.getMessage();
        }
        return io.daos.dfs.uns.DaosAcl.getDefaultInstance();
      }
    }

    /**
     * <code>.uns.DaosAcl pval = 5;</code>
     */
    public Builder setPval(io.daos.dfs.uns.DaosAcl value) {
      if (pvalBuilder_ == null) {
        if (value == null) {
          throw new NullPointerException();
        }
        value_ = value;
        onChanged();
      } else {
        pvalBuilder_.setMessage(value);
      }
      valueCase_ = 5;
      return this;
    }

    /**
     * <code>.uns.DaosAcl pval = 5;</code>
     */
    public Builder setPval(
        io.daos.dfs.uns.DaosAcl.Builder builderForValue) {
      if (pvalBuilder_ == null) {
        value_ = builderForValue.build();
        onChanged();
      } else {
        pvalBuilder_.setMessage(builderForValue.build());
      }
      valueCase_ = 5;
      return this;
    }

    /**
     * <code>.uns.DaosAcl pval = 5;</code>
     */
    public Builder mergePval(io.daos.dfs.uns.DaosAcl value) {
      if (pvalBuilder_ == null) {
        if (valueCase_ == 5 &&
            value_ != io.daos.dfs.uns.DaosAcl.getDefaultInstance()) {
          value_ = io.daos.dfs.uns.DaosAcl.newBuilder((io.daos.dfs.uns.DaosAcl) value_)
              .mergeFrom(value).buildPartial();
        } else {
          value_ = value;
        }
        onChanged();
      } else {
        if (valueCase_ == 5) {
          pvalBuilder_.mergeFrom(value);
        }
        pvalBuilder_.setMessage(value);
      }
      valueCase_ = 5;
      return this;
    }

    /**
     * <code>.uns.DaosAcl pval = 5;</code>
     */
    public Builder clearPval() {
      if (pvalBuilder_ == null) {
        if (valueCase_ == 5) {
          valueCase_ = 0;
          value_ = null;
          onChanged();
        }
      } else {
        if (valueCase_ == 5) {
          valueCase_ = 0;
          value_ = null;
        }
        pvalBuilder_.clear();
      }
      return this;
    }

    /**
     * <code>.uns.DaosAcl pval = 5;</code>
     */
    public io.daos.dfs.uns.DaosAcl.Builder getPvalBuilder() {
      return getPvalFieldBuilder().getBuilder();
    }

    /**
     * <code>.uns.DaosAcl pval = 5;</code>
     */
    public io.daos.dfs.uns.DaosAclOrBuilder getPvalOrBuilder() {
      if ((valueCase_ == 5) && (pvalBuilder_ != null)) {
        return pvalBuilder_.getMessageOrBuilder();
      } else {
        if (valueCase_ == 5) {
          return (io.daos.dfs.uns.DaosAcl) value_;
        }
        return io.daos.dfs.uns.DaosAcl.getDefaultInstance();
      }
    }

    /**
     * <code>.uns.DaosAcl pval = 5;</code>
     */
    private com.google.protobuf.SingleFieldBuilderV3<
        io.daos.dfs.uns.DaosAcl, io.daos.dfs.uns.DaosAcl.Builder, io.daos.dfs.uns.DaosAclOrBuilder>
        getPvalFieldBuilder() {
      if (pvalBuilder_ == null) {
        if (!(valueCase_ == 5)) {
          value_ = io.daos.dfs.uns.DaosAcl.getDefaultInstance();
        }
        pvalBuilder_ = new com.google.protobuf.SingleFieldBuilderV3<
            io.daos.dfs.uns.DaosAcl, io.daos.dfs.uns.DaosAcl.Builder, io.daos.dfs.uns.DaosAclOrBuilder>(
            (io.daos.dfs.uns.DaosAcl) value_,
            getParentForChildren(),
            isClean());
        value_ = null;
      }
      valueCase_ = 5;
      onChanged();
      ;
      return pvalBuilder_;
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


    // @@protoc_insertion_point(builder_scope:uns.Entry)
  }

  // @@protoc_insertion_point(class_scope:uns.Entry)
  private static final io.daos.dfs.uns.Entry DEFAULT_INSTANCE;

  static {
    DEFAULT_INSTANCE = new io.daos.dfs.uns.Entry();
  }

  public static io.daos.dfs.uns.Entry getDefaultInstance() {
    return DEFAULT_INSTANCE;
  }

  private static final com.google.protobuf.Parser<Entry>
      PARSER = new com.google.protobuf.AbstractParser<Entry>() {
    @java.lang.Override
    public Entry parsePartialFrom(
        com.google.protobuf.CodedInputStream input,
        com.google.protobuf.ExtensionRegistryLite extensionRegistry)
        throws com.google.protobuf.InvalidProtocolBufferException {
      return new Entry(input, extensionRegistry);
    }
  };

  public static com.google.protobuf.Parser<Entry> parser() {
    return PARSER;
  }

  @java.lang.Override
  public com.google.protobuf.Parser<Entry> getParserForType() {
    return PARSER;
  }

  @java.lang.Override
  public io.daos.dfs.uns.Entry getDefaultInstanceForType() {
    return DEFAULT_INSTANCE;
  }

}
