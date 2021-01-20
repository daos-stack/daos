// Generated by the protocol buffer compiler.  DO NOT EDIT!
// source: DaosObjectAttribute.proto

package io.daos.obj.attr;

/**
 * Protobuf type {@code objattr.DaosRpAttr}
 */
public final class DaosRpAttr extends
    com.google.protobuf.GeneratedMessageV3 implements
    // @@protoc_insertion_point(message_implements:objattr.DaosRpAttr)
    DaosRpAttrOrBuilder {
private static final long serialVersionUID = 0L;
  // Use DaosRpAttr.newBuilder() to construct.
  private DaosRpAttr(com.google.protobuf.GeneratedMessageV3.Builder<?> builder) {
    super(builder);
  }
  private DaosRpAttr() {
  }

  @Override
  @SuppressWarnings({"unused"})
  protected Object newInstance(
      UnusedPrivateParameter unused) {
    return new DaosRpAttr();
  }

  @Override
  public final com.google.protobuf.UnknownFieldSet
  getUnknownFields() {
    return this.unknownFields;
  }
  private DaosRpAttr(
      com.google.protobuf.CodedInputStream input,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws com.google.protobuf.InvalidProtocolBufferException {
    this();
    if (extensionRegistry == null) {
      throw new NullPointerException();
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

            rProto_ = input.readUInt32();
            break;
          }
          case 16: {

            rNum_ = input.readUInt32();
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
    return DaosObjAttrClasses.internal_static_objattr_DaosRpAttr_descriptor;
  }

  @Override
  protected FieldAccessorTable
      internalGetFieldAccessorTable() {
    return DaosObjAttrClasses.internal_static_objattr_DaosRpAttr_fieldAccessorTable
        .ensureFieldAccessorsInitialized(
            DaosRpAttr.class, Builder.class);
  }

  public static final int R_PROTO_FIELD_NUMBER = 1;
  private int rProto_;
  /**
   * <code>uint32 r_proto = 1;</code>
   * @return The rProto.
   */
  @Override
  public int getRProto() {
    return rProto_;
  }

  public static final int R_NUM_FIELD_NUMBER = 2;
  private int rNum_;
  /**
   * <code>uint32 r_num = 2;</code>
   * @return The rNum.
   */
  @Override
  public int getRNum() {
    return rNum_;
  }

  private byte memoizedIsInitialized = -1;
  @Override
  public final boolean isInitialized() {
    byte isInitialized = memoizedIsInitialized;
    if (isInitialized == 1) return true;
    if (isInitialized == 0) return false;

    memoizedIsInitialized = 1;
    return true;
  }

  @Override
  public void writeTo(com.google.protobuf.CodedOutputStream output)
                      throws java.io.IOException {
    if (rProto_ != 0) {
      output.writeUInt32(1, rProto_);
    }
    if (rNum_ != 0) {
      output.writeUInt32(2, rNum_);
    }
    unknownFields.writeTo(output);
  }

  @Override
  public int getSerializedSize() {
    int size = memoizedSize;
    if (size != -1) return size;

    size = 0;
    if (rProto_ != 0) {
      size += com.google.protobuf.CodedOutputStream
        .computeUInt32Size(1, rProto_);
    }
    if (rNum_ != 0) {
      size += com.google.protobuf.CodedOutputStream
        .computeUInt32Size(2, rNum_);
    }
    size += unknownFields.getSerializedSize();
    memoizedSize = size;
    return size;
  }

  @Override
  public boolean equals(final Object obj) {
    if (obj == this) {
     return true;
    }
    if (!(obj instanceof DaosRpAttr)) {
      return super.equals(obj);
    }
    DaosRpAttr other = (DaosRpAttr) obj;

    if (getRProto()
        != other.getRProto()) return false;
    if (getRNum()
        != other.getRNum()) return false;
    if (!unknownFields.equals(other.unknownFields)) return false;
    return true;
  }

  @Override
  public int hashCode() {
    if (memoizedHashCode != 0) {
      return memoizedHashCode;
    }
    int hash = 41;
    hash = (19 * hash) + getDescriptor().hashCode();
    hash = (37 * hash) + R_PROTO_FIELD_NUMBER;
    hash = (53 * hash) + getRProto();
    hash = (37 * hash) + R_NUM_FIELD_NUMBER;
    hash = (53 * hash) + getRNum();
    hash = (29 * hash) + unknownFields.hashCode();
    memoizedHashCode = hash;
    return hash;
  }

  public static DaosRpAttr parseFrom(
      java.nio.ByteBuffer data)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data);
  }
  public static DaosRpAttr parseFrom(
      java.nio.ByteBuffer data,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data, extensionRegistry);
  }
  public static DaosRpAttr parseFrom(
      com.google.protobuf.ByteString data)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data);
  }
  public static DaosRpAttr parseFrom(
      com.google.protobuf.ByteString data,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data, extensionRegistry);
  }
  public static DaosRpAttr parseFrom(byte[] data)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data);
  }
  public static DaosRpAttr parseFrom(
      byte[] data,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws com.google.protobuf.InvalidProtocolBufferException {
    return PARSER.parseFrom(data, extensionRegistry);
  }
  public static DaosRpAttr parseFrom(java.io.InputStream input)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseWithIOException(PARSER, input);
  }
  public static DaosRpAttr parseFrom(
      java.io.InputStream input,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseWithIOException(PARSER, input, extensionRegistry);
  }
  public static DaosRpAttr parseDelimitedFrom(java.io.InputStream input)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseDelimitedWithIOException(PARSER, input);
  }
  public static DaosRpAttr parseDelimitedFrom(
      java.io.InputStream input,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseDelimitedWithIOException(PARSER, input, extensionRegistry);
  }
  public static DaosRpAttr parseFrom(
      com.google.protobuf.CodedInputStream input)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseWithIOException(PARSER, input);
  }
  public static DaosRpAttr parseFrom(
      com.google.protobuf.CodedInputStream input,
      com.google.protobuf.ExtensionRegistryLite extensionRegistry)
      throws java.io.IOException {
    return com.google.protobuf.GeneratedMessageV3
        .parseWithIOException(PARSER, input, extensionRegistry);
  }

  @Override
  public Builder newBuilderForType() { return newBuilder(); }
  public static Builder newBuilder() {
    return DEFAULT_INSTANCE.toBuilder();
  }
  public static Builder newBuilder(DaosRpAttr prototype) {
    return DEFAULT_INSTANCE.toBuilder().mergeFrom(prototype);
  }
  @Override
  public Builder toBuilder() {
    return this == DEFAULT_INSTANCE
        ? new Builder() : new Builder().mergeFrom(this);
  }

  @Override
  protected Builder newBuilderForType(
      BuilderParent parent) {
    Builder builder = new Builder(parent);
    return builder;
  }
  /**
   * Protobuf type {@code objattr.DaosRpAttr}
   */
  public static final class Builder extends
      com.google.protobuf.GeneratedMessageV3.Builder<Builder> implements
      // @@protoc_insertion_point(builder_implements:objattr.DaosRpAttr)
      DaosRpAttrOrBuilder {
    public static final com.google.protobuf.Descriptors.Descriptor
        getDescriptor() {
      return DaosObjAttrClasses.internal_static_objattr_DaosRpAttr_descriptor;
    }

    @Override
    protected FieldAccessorTable
        internalGetFieldAccessorTable() {
      return DaosObjAttrClasses.internal_static_objattr_DaosRpAttr_fieldAccessorTable
          .ensureFieldAccessorsInitialized(
              DaosRpAttr.class, Builder.class);
    }

    // Construct using io.daos.obj.attr.DaosRpAttr.newBuilder()
    private Builder() {
      maybeForceBuilderInitialization();
    }

    private Builder(
        BuilderParent parent) {
      super(parent);
      maybeForceBuilderInitialization();
    }
    private void maybeForceBuilderInitialization() {
      if (com.google.protobuf.GeneratedMessageV3
              .alwaysUseFieldBuilders) {
      }
    }
    @Override
    public Builder clear() {
      super.clear();
      rProto_ = 0;

      rNum_ = 0;

      return this;
    }

    @Override
    public com.google.protobuf.Descriptors.Descriptor
        getDescriptorForType() {
      return DaosObjAttrClasses.internal_static_objattr_DaosRpAttr_descriptor;
    }

    @Override
    public DaosRpAttr getDefaultInstanceForType() {
      return DaosRpAttr.getDefaultInstance();
    }

    @Override
    public DaosRpAttr build() {
      DaosRpAttr result = buildPartial();
      if (!result.isInitialized()) {
        throw newUninitializedMessageException(result);
      }
      return result;
    }

    @Override
    public DaosRpAttr buildPartial() {
      DaosRpAttr result = new DaosRpAttr(this);
      result.rProto_ = rProto_;
      result.rNum_ = rNum_;
      onBuilt();
      return result;
    }

    @Override
    public Builder clone() {
      return super.clone();
    }
    @Override
    public Builder setField(
        com.google.protobuf.Descriptors.FieldDescriptor field,
        Object value) {
      return super.setField(field, value);
    }
    @Override
    public Builder clearField(
        com.google.protobuf.Descriptors.FieldDescriptor field) {
      return super.clearField(field);
    }
    @Override
    public Builder clearOneof(
        com.google.protobuf.Descriptors.OneofDescriptor oneof) {
      return super.clearOneof(oneof);
    }
    @Override
    public Builder setRepeatedField(
        com.google.protobuf.Descriptors.FieldDescriptor field,
        int index, Object value) {
      return super.setRepeatedField(field, index, value);
    }
    @Override
    public Builder addRepeatedField(
        com.google.protobuf.Descriptors.FieldDescriptor field,
        Object value) {
      return super.addRepeatedField(field, value);
    }
    @Override
    public Builder mergeFrom(com.google.protobuf.Message other) {
      if (other instanceof DaosRpAttr) {
        return mergeFrom((DaosRpAttr)other);
      } else {
        super.mergeFrom(other);
        return this;
      }
    }

    public Builder mergeFrom(DaosRpAttr other) {
      if (other == DaosRpAttr.getDefaultInstance()) return this;
      if (other.getRProto() != 0) {
        setRProto(other.getRProto());
      }
      if (other.getRNum() != 0) {
        setRNum(other.getRNum());
      }
      this.mergeUnknownFields(other.unknownFields);
      onChanged();
      return this;
    }

    @Override
    public final boolean isInitialized() {
      return true;
    }

    @Override
    public Builder mergeFrom(
        com.google.protobuf.CodedInputStream input,
        com.google.protobuf.ExtensionRegistryLite extensionRegistry)
        throws java.io.IOException {
      DaosRpAttr parsedMessage = null;
      try {
        parsedMessage = PARSER.parsePartialFrom(input, extensionRegistry);
      } catch (com.google.protobuf.InvalidProtocolBufferException e) {
        parsedMessage = (DaosRpAttr) e.getUnfinishedMessage();
        throw e.unwrapIOException();
      } finally {
        if (parsedMessage != null) {
          mergeFrom(parsedMessage);
        }
      }
      return this;
    }

    private int rProto_ ;
    /**
     * <code>uint32 r_proto = 1;</code>
     * @return The rProto.
     */
    @Override
    public int getRProto() {
      return rProto_;
    }
    /**
     * <code>uint32 r_proto = 1;</code>
     * @param value The rProto to set.
     * @return This builder for chaining.
     */
    public Builder setRProto(int value) {

      rProto_ = value;
      onChanged();
      return this;
    }
    /**
     * <code>uint32 r_proto = 1;</code>
     * @return This builder for chaining.
     */
    public Builder clearRProto() {

      rProto_ = 0;
      onChanged();
      return this;
    }

    private int rNum_ ;
    /**
     * <code>uint32 r_num = 2;</code>
     * @return The rNum.
     */
    @Override
    public int getRNum() {
      return rNum_;
    }
    /**
     * <code>uint32 r_num = 2;</code>
     * @param value The rNum to set.
     * @return This builder for chaining.
     */
    public Builder setRNum(int value) {

      rNum_ = value;
      onChanged();
      return this;
    }
    /**
     * <code>uint32 r_num = 2;</code>
     * @return This builder for chaining.
     */
    public Builder clearRNum() {

      rNum_ = 0;
      onChanged();
      return this;
    }
    @Override
    public final Builder setUnknownFields(
        final com.google.protobuf.UnknownFieldSet unknownFields) {
      return super.setUnknownFields(unknownFields);
    }

    @Override
    public final Builder mergeUnknownFields(
        final com.google.protobuf.UnknownFieldSet unknownFields) {
      return super.mergeUnknownFields(unknownFields);
    }


    // @@protoc_insertion_point(builder_scope:objattr.DaosRpAttr)
  }

  // @@protoc_insertion_point(class_scope:objattr.DaosRpAttr)
  private static final DaosRpAttr DEFAULT_INSTANCE;
  static {
    DEFAULT_INSTANCE = new DaosRpAttr();
  }

  public static DaosRpAttr getDefaultInstance() {
    return DEFAULT_INSTANCE;
  }

  private static final com.google.protobuf.Parser<DaosRpAttr>
      PARSER = new com.google.protobuf.AbstractParser<DaosRpAttr>() {
    @Override
    public DaosRpAttr parsePartialFrom(
        com.google.protobuf.CodedInputStream input,
        com.google.protobuf.ExtensionRegistryLite extensionRegistry)
        throws com.google.protobuf.InvalidProtocolBufferException {
      return new DaosRpAttr(input, extensionRegistry);
    }
  };

  public static com.google.protobuf.Parser<DaosRpAttr> parser() {
    return PARSER;
  }

  @Override
  public com.google.protobuf.Parser<DaosRpAttr> getParserForType() {
    return PARSER;
  }

  @Override
  public DaosRpAttr getDefaultInstanceForType() {
    return DEFAULT_INSTANCE;
  }

}

