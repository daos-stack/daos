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

package io.daos.dfs;

import java.io.File;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.HashMap;
import java.util.Map;

import com.google.protobuf.TextFormat;

import io.daos.*;
import io.daos.dfs.uns.*;

import org.apache.commons.lang.ObjectUtils;
import org.apache.commons.lang.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import sun.nio.ch.DirectBuffer;

/**
 * A wrapper class of DAOS Unified Namespace. There are four DAOS UNS methods,
 * {@link #createPath()}, {@link #resolvePath(String)}, {@link #destroyPath()} and
 * {@link #parseAttribute(String)}, wrapped in this class.
 *
 * <p>
 * Due to complexity of DAOS UNS attribute, duns_attr_t, protobuf and c plugin, protobuf-c, are introduced to
 * pass parameters accurately and efficiently. check DunsAttribute.proto and its auto-generated classes under
 * package io.daos.dfs.uns.
 *
 * <p>
 * The typical usage is,
 * 1, create path
 * <code>
 * DaosUns.DaosUnsBuilder builder = new DaosUns.DaosUnsBuilder();
 * builder.path(file.getAbsolutePath());
 * builder.poolId(poolId);
 * // set more parameters
 * ...
 * DaosUns uns = builder.build();
 * String cid = uns.createPath();
 * </code>
 *
 * <p>
 * 2, resolve path
 * <code>
 * DunsAttribute attribute = DaosUns.resolvePath(file.getAbsolutePath());
 * </code>
 *
 * <p>
 * 3, check DaosUnsIT for more complex usage
 */
public class DaosUns {

  private DaosUnsBuilder builder;

  private DunsAttribute attribute;

  private static final Logger log = LoggerFactory.getLogger(DaosUns.class);

  private DaosUns() {
  }

  /**
   * create UNS path with info of type, pool UUID and container UUID set.
   * A new container will be created with some properties from {@link DaosUnsBuilder}.
   *
   * @return container UUID
   * @throws IOException
   * {@link DaosIOException}
   */
  public String createPath() throws IOException {
    long poolHandle;

    if (attribute == null) {
      throw new IllegalStateException("DUNS attribute is not set");
    }
    byte[] bytes = attribute.toByteArray();
    ByteBuffer buffer = BufferAllocator.directBuffer(bytes.length);
    buffer.put(bytes);
    poolHandle = DaosClient.daosOpenPool(builder.poolUuid, builder.serverGroup,
        builder.ranks, builder.poolFlags);
    try {
      String cuuid = DaosFsClient.dunsCreatePath(poolHandle, builder.path,
          ((DirectBuffer) buffer).address(), bytes.length);
      log.info("UNS path {} created in pool {} and container {}",
          builder.path, builder.poolUuid, cuuid);
      return cuuid;
    } finally {
      if (poolHandle != 0) {
        DaosClient.daosClosePool(poolHandle);
      }
    }
  }

  /**
   * extract and parse extended attributes from given <code>path</code>.
   *
   * @param path OS file path
   * @return UNS attribute
   * @throws IOException
   * {@link DaosIOException}
   */
  public static DunsAttribute resolvePath(String path) throws IOException {
    byte[] bytes = DaosFsClient.dunsResolvePath(path);
    if (bytes == null) {
      return null;
    }
    return DunsAttribute.parseFrom(bytes);
  }

  /**
   * set application info to <code>attrName</code> on <code>path</code>.
   * If <code>value</code> is empty, the <code>attrName</code> will be removed from the path.
   *
   * @param path     OS file path
   * @param attrName attribute name. Its length should not exceed {@value Constants#UNS_ATTR_NAME_MAX_LEN}.
   * @param value    attribute value, Its length should not exceed {@value Constants#UNS_ATTR_VALUE_MAX_LEN}.
   * @throws IOException
   * {@link DaosIOException}
   */
  public static void setAppInfo(String path, String attrName, String value) throws IOException {
    if (StringUtils.isBlank(attrName)) {
      throw new IllegalArgumentException("attribute name cannot be empty");
    }
    if (!attrName.startsWith("user.")) {
      throw new IllegalArgumentException("attribute name should start with \"user.\", " + attrName);
    }
    if (attrName.length() > Constants.UNS_ATTR_NAME_MAX_LEN) {
      throw new IllegalArgumentException("attribute name " + attrName + ", length should not exceed " +
          Constants.UNS_ATTR_NAME_MAX_LEN);
    }
    if (value != null && value.length() > Constants.UNS_ATTR_VALUE_MAX_LEN) {
      throw new IllegalArgumentException("attribute value length should not exceed " +
          Constants.UNS_ATTR_VALUE_MAX_LEN);
    }
    DaosFsClient.dunsSetAppInfo(path, attrName, value);
  }

  /**
   * get application info stored in <code>attrName</code> from <code>path</code>.
   *
   * @param path        OS file path
   * @param attrName    attribute name. Its length should not exceed {@value Constants#UNS_ATTR_NAME_MAX_LEN}.
   * @param maxValueLen maximum attribute length. It should not exceed {@value Constants#UNS_ATTR_VALUE_MAX_LEN}.
   * @return attribute value
   * @throws IOException
   * {@link DaosIOException}
   */
  public static String getAppInfo(String path, String attrName, int maxValueLen) throws IOException {
    if (StringUtils.isBlank(attrName)) {
      throw new IllegalArgumentException("attribute name cannot be empty");
    }
    if (!attrName.startsWith("user.")) {
      throw new IllegalArgumentException("attribute name should start with \"user.\", " + attrName);
    }
    if (attrName.length() > Constants.UNS_ATTR_NAME_MAX_LEN) {
      throw new IllegalArgumentException("attribute name " + attrName + ", length should not exceed " +
          Constants.UNS_ATTR_NAME_MAX_LEN);
    }
    if (maxValueLen > Constants.UNS_ATTR_VALUE_MAX_LEN) {
      throw new IllegalArgumentException("maximum value length should not exceed " +
          Constants.UNS_ATTR_VALUE_MAX_LEN);
    }
    return DaosFsClient.dunsGetAppInfo(path, attrName, maxValueLen);
  }

  /**
   * Destroy a container and remove the path associated with it in the UNS.
   *
   * @throws IOException
   * {@link DaosIOException}
   */
  public void destroyPath() throws IOException {
    long poolHandle = 0;

    poolHandle = DaosClient.daosOpenPool(builder.poolUuid, builder.serverGroup,
        builder.ranks, builder.poolFlags);
    try {
      DaosFsClient.dunsDestroyPath(poolHandle, builder.path);
      log.info("UNS path {} destroyed");
    } finally {
      if (poolHandle != 0) {
        DaosClient.daosClosePool(poolHandle);
      }
    }
  }

  /**
   * parse input string to UNS attribute.
   *
   * @param input attribute string
   * @return UNS attribute
   * @throws IOException
   * {@link DaosIOException}
   */
  public static DunsAttribute parseAttribute(String input) throws IOException {
    byte[] bytes = DaosFsClient.dunsParseAttribute(input);
    return DunsAttribute.parseFrom(bytes);
  }

  /**
   * get information from extended attributes of UNS path.
   * Some info gets from DAOS extended attribute.
   * The Rest gets from app extended attributes if any.
   *
   * @param path            OS FS path or path prefixed with the UUIDs
   * @param appInfoAttrName app-specific attribute name
   * @return information hold in {@link DunsInfo}
   * @throws IOException
   * {@link DaosIOException}
   */
  public static DunsInfo getAccessInfo(String path, String appInfoAttrName) throws IOException {
    return getAccessInfo(path, appInfoAttrName, Constants.UNS_ATTR_VALUE_MAX_LEN_DEFAULT,
        false);
  }

  /**
   * get information from extended attributes of UNS path.
   * Some info gets from DAOS extended attribute.
   * The Rest gets from app extended attributes. A exception will be thrown if user expect app info and no
   * info gets.
   *
   * @param path            OS FS path or path prefixed with the UUIDs
   * @param appInfoAttrName app-specific attribute name
   * @param maxValueLen     maximum value length
   * @param expectAppInfo   expect app info? true for throwing exception if no value gets, false for ignoring quietly.
   * @return information hold in {@link DunsInfo}
   * @throws IOException
   * {@link DaosIOException}
   */
  public static DunsInfo getAccessInfo(String path, String appInfoAttrName, int maxValueLen,
                                       boolean expectAppInfo) throws IOException {
    DunsAttribute attribute = DaosUns.resolvePath(path);
    if (attribute == null) {
      throw new IOException("no UNS attribute get from " + path);
    }
    String poolId = attribute.getPuuid();
    String contId = attribute.getCuuid();
    Layout layout = attribute.getLayoutType();
    String prefix = path;
    String value = null;
    String idPrefix = "/" + poolId + "/" + contId;
    if (path.startsWith(idPrefix)) {
      prefix = idPrefix;
      if (layout == Layout.UNKNOWN) {
        layout = Layout.POSIX; // default to posix
      }
    } else {
      try {
        value = DaosUns.getAppInfo(path, appInfoAttrName,
            maxValueLen);
      } catch (DaosIOException e) {
        if (expectAppInfo) {
          throw e;
        }
      }
    }
    return new DunsInfo(poolId, contId, layout.name(), value, prefix);
  }

  protected DunsAttribute getAttribute() {
    return attribute;
  }

  public String getPath() {
    return builder.path;
  }

  public String getPoolUuid() {
    return builder.poolUuid;
  }

  public String getContUuid() {
    return builder.contUuid;
  }

  public Layout getLayout() {
    return builder.layout;
  }

  public DaosObjectType getObjectType() {
    return builder.objectType;
  }

  public long getChunkSize() {
    return builder.chunkSize;
  }

  public boolean isOnLustre() {
    return builder.onLustre;
  }

  public PropValue getProperty(PropType type) {
    return builder.propMap.get(type);
  }

  /**
   * A builder class to build {@link DaosUns} instance. Most of methods are same as ones
   * in {@link io.daos.dfs.DaosFsClient.DaosFsClientBuilder}, like {@link #ranks(String)},
   * {@link #serverGroup(String)}, {@link #poolFlags(int)}.
   *
   * <p>
   * For other methods, they are specific for DAOS UNS, like {@link #layout(Layout)} and
   * {@link #putEntry(PropType, PropValue)}. Some parameters are of types auto-generated
   * by protobuf-c.
   */
  public static class DaosUnsBuilder implements Cloneable {
    private String path;
    private String poolUuid;
    private String contUuid;
    private Layout layout = Layout.POSIX;
    private DaosObjectType objectType = DaosObjectType.OC_SX;
    private long chunkSize = Constants.FILE_DEFAULT_CHUNK_SIZE;
    private boolean onLustre;
    private Map<PropType, PropValue> propMap = new HashMap<>();
    private int propReserved;

    private String ranks = Constants.POOL_DEFAULT_RANKS;
    private String serverGroup = Constants.POOL_DEFAULT_SERVER_GROUP;
    private int poolFlags = Constants.ACCESS_FLAG_POOL_READWRITE;

    /**
     * file denoted by <code>path</code> should not exist.
     *
     * @param path OS file path extended attributes associated with
     * @return this object
     */
    public DaosUnsBuilder path(String path) {
      this.path = path;
      return this;
    }

    /**
     * set pool UUID.
     *
     * @param poolUuid
     * pool uuid
     * @return this object
     */
    public DaosUnsBuilder poolId(String poolUuid) {
      this.poolUuid = poolUuid;
      return this;
    }

    /**
     * set container UUID.
     *
     * @param contUuid
     * container uuid
     * @return this object
     */
    public DaosUnsBuilder containerId(String contUuid) {
      this.contUuid = contUuid;
      return this;
    }

    /**
     * set layout.
     *
     * @param layout
     * posix or hdf5 layout
     * @return this object
     */
    public DaosUnsBuilder layout(Layout layout) {
      this.layout = layout;
      return this;
    }

    /**
     * set object type.
     *
     * @param objectType
     * object type
     * @return this object
     */
    public DaosUnsBuilder objectType(DaosObjectType objectType) {
      this.objectType = objectType;
      return this;
    }

    /**
     * set chunk size.
     *
     * @param chunkSize
     * chunk size
     * @return this object
     */
    public DaosUnsBuilder chunkSize(long chunkSize) {
      if (chunkSize < 0) {
        throw new IllegalArgumentException("chunk size should be positive integer");
      }
      this.chunkSize = chunkSize;
      return this;
    }

    /**
     * set if it's on lustre FS.
     *
     * @param onLustre
     * true for on lustre, false otherwise.
     * @return this object
     */
    public DaosUnsBuilder onLustre(boolean onLustre) {
      this.onLustre = onLustre;
      return this;
    }

    /**
     * put entry as type-value pair. For <code>value</code>, there is method
     * {@link PropValue#getValueClass(PropType)} for you to get correct value class.
     *
     * @param propType enum values of {@link PropType}
     * @param value    value object
     * @return this object
     */
    public DaosUnsBuilder putEntry(PropType propType, PropValue value) {
      switch (propType) {
        case DAOS_PROP_PO_MIN:
        case DAOS_PROP_PO_MAX:
        case DAOS_PROP_CO_MIN:
        case DAOS_PROP_CO_MAX:
          throw new IllegalArgumentException("invalid property type: " + propType);
        default: break;
      }
      propMap.put(propType, value);
      return this;
    }

    public DaosUnsBuilder propReserved(int propReserved) {
      this.propReserved = propReserved;
      return this;
    }

    public DaosUnsBuilder ranks(String ranks) {
      this.ranks = ranks;
      return this;
    }

    public DaosUnsBuilder serverGroup(String serverGroup) {
      this.serverGroup = serverGroup;
      return this;
    }

    public DaosUnsBuilder poolFlags(int poolFlags) {
      this.poolFlags = poolFlags;
      return this;
    }

    @Override
    public DaosUnsBuilder clone() throws CloneNotSupportedException {
      return (DaosUnsBuilder) super.clone();
    }

    /**
     * verify and map parameters to UNS attribute objects whose classes are auto-generated by protobuf-c.
     * Then, create {@link DaosUns} object with the UNS attribute, which is to be serialized when interact
     * with native code.
     *
     * @return {@link DaosUns} object
     */
    public DaosUns build() {
      if (path == null) {
        throw new IllegalArgumentException("need path");
      }
      if (poolUuid == null) {
        throw new IllegalArgumentException("need pool UUID");
      }
      if (layout == Layout.UNKNOWN || layout == Layout.UNRECOGNIZED) {
        throw new IllegalArgumentException("layout should be posix or HDF5");
      }
      DaosUns duns = new DaosUns();
      duns.builder = (DaosUnsBuilder) ObjectUtils.clone(this);
      buildAttribute(duns);
      return duns;
    }

    private void buildAttribute(DaosUns duns) {
      DunsAttribute.Builder builder = DunsAttribute.newBuilder();
      builder.setPuuid(poolUuid);
      if (contUuid != null) {
        builder.setCuuid(contUuid);
      }
      builder.setLayoutType(layout);
      builder.setObjectType(objectType.nameWithoutOc());
      builder.setChunkSize(chunkSize);
      builder.setOnLustre(onLustre);
      buildProperties(builder);
      duns.attribute = builder.build();
    }

    private void buildProperties(DunsAttribute.Builder attrBuilder) {
      Properties.Builder builder = setProperties(propReserved, propMap);
      if (builder != null) {
        attrBuilder.setProperties(builder.build());
      }
    }

    private static Properties.Builder setProperties(int propReserved, Map<PropType, PropValue> propMap) {
      if (propMap.isEmpty()) {
        return null;
      }
      Properties.Builder builder = Properties.newBuilder();
      builder.setReserved(propReserved);
      Entry.Builder eb = Entry.newBuilder();
      for (Map.Entry<PropType, PropValue> entry : propMap.entrySet()) {
        eb.clear();
        eb.setType(entry.getKey()).setReserved(entry.getValue().getReserved());
        Class<?> valueClass = PropValue.getValueClass(entry.getKey());
        if (valueClass == Long.class) {
          eb.setVal((Long) entry.getValue().getValue());
        } else if (valueClass == String.class) {
          eb.setStr((String) entry.getValue().getValue());
        } else {
          eb.setPval((DaosAcl) entry.getValue().getValue());
        }
        builder.addEntries(eb.build());
      }
      return builder;
    }
  }

  /**
   * A property value class of corresponding {@link PropType}.
   * The actual value classes can be determined by call {@link #getValueClass(PropType)}.
   * Currently, there are three value classes, {@link Long}, {@link String} and {@link DaosAcl}.
   */
  public static class PropValue {
    private int reserved;
    private Object value;

    public PropValue(Object value, int reserved) {
      this.reserved = reserved;
      this.value = value;
    }

    public Object getValue() {
      return value;
    }

    public int getReserved() {
      return reserved;
    }

    /**
     * get correct Java class for DAOS property type.
     *
     * @param propType
     * DAOS property type
     * @return Java class
     */
    public static Class<?> getValueClass(PropType propType) {
      switch (propType) {
        case DAOS_PROP_PO_SPACE_RB:
        case DAOS_PROP_CO_LAYOUT_VER:
        case DAOS_PROP_CO_LAYOUT_TYPE:
        case DAOS_PROP_CO_CSUM_CHUNK_SIZE:
        case DAOS_PROP_CO_CSUM_SERVER_VERIFY:
        case DAOS_PROP_CO_CSUM:
        case DAOS_PROP_CO_REDUN_FAC:
        case DAOS_PROP_CO_REDUN_LVL:
        case DAOS_PROP_CO_SNAPSHOT_MAX:
          return Long.class;
        case DAOS_PROP_PO_LABEL:
        case DAOS_PROP_PO_SELF_HEAL:
        case DAOS_PROP_PO_RECLAIM:
        case DAOS_PROP_PO_OWNER:
        case DAOS_PROP_PO_OWNER_GROUP:
        case DAOS_PROP_PO_SVC_LIST:
        case DAOS_PROP_CO_LABEL:
        case DAOS_PROP_CO_COMPRESS:
        case DAOS_PROP_CO_ENCRYPT:
        case DAOS_PROP_CO_OWNER:
        case DAOS_PROP_CO_OWNER_GROUP:
          return String.class;
        case DAOS_PROP_PO_ACL:
        case DAOS_PROP_CO_ACL:
          return DaosAcl.class;
        default:
          throw new IllegalArgumentException("no value class for " + propType);
      }
    }
  }

  /**
   * Main function to be called from command line.
   *
   * @param args
   * command line arguments.
   * @throws Exception
   * any exception during execution
   */
  public static void main(String[] args) throws Exception {
    if (needUsage(args)) {
      String usage = getUsage();
      log.info(usage);
      return;
    }
    if (args.length < 1) {
      throw new IllegalArgumentException("need one of commands" +
          " [create|resolve|destroy|parse|util]\n" + getUsage());
    }
    switch (args[0]) {
      case "create":
        create();
        return;
      case "resolve":
        resolve();
        return;
      case "destroy":
        destroy();
        return;
      case "parse":
        parse();
        return;
      case "setappinfo":
        setAppInfoCommand();
        return;
      case "getappinfo":
        getAppInfoCommand();
        return;
      case "util":
        util();
        return;
      default:
        throw new IllegalArgumentException("not supported command, " + args[0]);
    }
  }

  private static void setAppInfoCommand() {
    String path = System.getProperty("path");
    String attr = System.getProperty("attr");
    String value = System.getProperty("value");
    try {
      setAppInfo(path, attr, value);
      log.info("attribute({}) = value({}) is set on path({})", attr, value, path);
    } catch (Exception e) {
      log.error("failed to set app info. ", e);
    }
  }

  private static void getAppInfoCommand() {
    String path = System.getProperty("path");
    String attr = System.getProperty("attr");
    String maxLen = System.getProperty("maxlen");
    try {
      String value = getAppInfo(path, attr, maxLen == null ? Constants.UNS_ATTR_VALUE_MAX_LEN_DEFAULT :
          Integer.valueOf(maxLen));
      log.info("attribute({}) = value({}) get from path({})", attr, value, path);
    } catch (Exception e) {
      log.error("failed to get app info. ", e);
    }
  }

  private static void util() {
    String op = System.getProperty("op");
    if (StringUtils.isBlank(op)) {
      throw new IllegalArgumentException("need operation type.\n" + getUsage());
    }
    switch (op) {
      case "list-object-types":
        listObjectTypes();
        return;
      case "list-property-types":
        listPropertyTypes();
        return;
      case "get-property-value-type":
        getPropertyValueType();
        return;
      case "sample-properties":
        sampleProperties();
        return;
      case "escape-app-value":
        escapeAppValue();
        return;
      default:
        throw new IllegalArgumentException("not supported operation, " + op);
    }
  }

  private static void listObjectTypes() {
    StringBuilder sb = new StringBuilder();
    sb.append("object types:\n");
    for (DaosObjectType type : DaosObjectType.values()) {
      sb.append(type).append("\n");
    }
    log.info(sb.toString());
  }

  private static void listPropertyTypes() {
    StringBuilder sb = new StringBuilder();
    sb.append("property types:\n");
    for (PropType type : PropType.values()) {
      sb.append(type).append("\n");
    }
    log.info(sb.toString());
  }

  private static void getPropertyValueType() {
    String type = System.getProperty("prop_type");
    if (StringUtils.isBlank(type)) {
      throw new IllegalArgumentException("need property type.\n" + getUsage());
    }
    log.info("property value type is: " + PropValue.getValueClass(PropType.valueOf(type)));
  }

  private static void sampleProperties() {
    log.info("sample 1, Set Integer Value:");
    printPropIntValue();
    log.info("sample 2, Set String Value:");
    printPropStringValue();
    log.info("sample 3, Set Two ACL Values:");
    printPropAclValues();
    log.info("sample 4, Set Integer And ACL Values:");
    printPropIntAndAclValues();
  }

  private static void escapeAppValue() {
    String input = System.getProperty("input");
    if (StringUtils.isBlank(input)) {
      throw new IllegalArgumentException("need input.\n" + getUsage());
    }
    log.info("escaped value is: " + DaosUtils.escapeUnsValue(input));
  }

  private static String escapeDoubleQuote(String str) {
    return str.replaceAll("\"", "\\\\\"");
  }

  private static void printPropIntValue() {
    Map<PropType, DaosUns.PropValue> propMap = new HashMap<>();
    propMap.put(PropType.DAOS_PROP_CO_LAYOUT_VER, new DaosUns.PropValue(2L, 0));
    Properties.Builder builder = DaosUnsBuilder.setProperties(1, propMap);
    String str = TextFormat.shortDebugString(builder);
    log.info("-Dproperties=\"" + escapeDoubleQuote(str) + "\"");
  }

  private static void printPropStringValue() {
    Map<PropType, DaosUns.PropValue> propMap = new HashMap<>();
    propMap.put(PropType.DAOS_PROP_CO_LABEL, new DaosUns.PropValue("label", 0));
    Properties.Builder builder = DaosUnsBuilder.setProperties(0, propMap);
    String str = TextFormat.shortDebugString(builder);
    log.info("-Dproperties=\"" + escapeDoubleQuote(str) + "\"");
  }

  private static void printPropAclValues() {
    Map<PropType, DaosUns.PropValue> propMap = new HashMap<>();
    int aclOwner = 0;
    int aclUser = 1;
    String user = new com.sun.security.auth.module.UnixSystem().getUsername() + "@";
    int accessAllow = 1;
    int permDel = 1 << 3;
    int permGet = 1 << 6;
    int permSet = 1 << 7;
    int perms = permGet | permDel | permSet;
    DaosAce ace = DaosAce.newBuilder()
        .setAccessTypes(accessAllow)
        .setPrincipal(user)
        .setPrincipalType(aclUser)
        .setPrincipalLen(user.length())
        .setAllowPerms(perms)
        .build();
    DaosAce ace2 = DaosAce.newBuilder()
        .setAccessTypes(accessAllow)
        .setPrincipalType(aclOwner)
        .setAllowPerms(perms)
        .setPrincipalLen(0)
        .build();
    DaosAcl.Builder aclBuilder = DaosAcl.newBuilder()
        .setVer(1);
    aclBuilder.addAces(ace2).addAces(ace);
    DaosAcl acl = aclBuilder.build();
    propMap.put(PropType.DAOS_PROP_CO_ACL, new DaosUns.PropValue(acl, 1));
    Properties.Builder builder = DaosUnsBuilder.setProperties(0, propMap);
    String str = TextFormat.shortDebugString(builder);
    log.info("-Dproperties=\"" + escapeDoubleQuote(str) + "\"");
  }

  private static void printPropIntAndAclValues() {
    Map<PropType, DaosUns.PropValue> propMap = new HashMap<>();
    int aclUser = 1;
    String user = new com.sun.security.auth.module.UnixSystem().getUsername() + "@";
    int accessAllow = 1;
    int permDel = 1 << 3;
    int permGet = 1 << 6;
    int permSet = 1 << 7;
    int perms = permGet | permDel | permSet;
    DaosAce ace = DaosAce.newBuilder()
        .setAccessTypes(accessAllow)
        .setPrincipal(user)
        .setPrincipalType(aclUser)
        .setPrincipalLen(user.length())
        .setAllowPerms(perms)
        .build();
    DaosAcl.Builder aclBuilder = DaosAcl.newBuilder()
        .setVer(1);
    aclBuilder.addAces(ace);
    DaosAcl acl = aclBuilder.build();
    propMap.put(PropType.DAOS_PROP_CO_LAYOUT_VER, new DaosUns.PropValue(2L, 0));
    propMap.put(PropType.DAOS_PROP_CO_ACL, new DaosUns.PropValue(acl, 1));
    Properties.Builder builder = DaosUnsBuilder.setProperties(0, propMap);
    String str = TextFormat.shortDebugString(builder);
    log.info("-Dproperties=\"" + escapeDoubleQuote(str) + "\"");
  }

  private static void parse() {
    String input = System.getProperty("input");
    try {
      DunsAttribute attribute = DaosUns.parseAttribute(input);
      log.info("parsed attributes from string: " + input);
      log.info(TextFormat.printer().printToString(attribute));
    } catch (Exception e) {
      log.error("failed to parse attribute string.", e);
    }
  }

  private static void resolve() {
    String path = System.getProperty("path");
    try {
      DunsAttribute attribute = DaosUns.resolvePath(path);
      log.info("resolved attributes from UNS path: " + path);
      log.info(TextFormat.printer().printToString(attribute));
    } catch (Exception e) {
      log.error("failed to resolve path. ", e);
    }
  }

  private static void destroy() {
    String path = System.getProperty("path");
    String poolId = System.getProperty("pool_id");
    String ranks = System.getProperty("ranks");
    String serverGrp = System.getProperty("server_group");
    String poolFlags = System.getProperty("pool_flags");

    if (StringUtils.isBlank(path)) {
      throw new IllegalArgumentException("need path, -Dpath=");
    }
    if (StringUtils.isBlank(poolId)) {
      throw new IllegalArgumentException("need pool UUID, -Dpool_id");
    }

    File file = new File(path);
    DaosUns.DaosUnsBuilder builder = new DaosUns.DaosUnsBuilder();
    builder.path(file.getAbsolutePath());
    builder.poolId(poolId);

    if (!StringUtils.isBlank(ranks)) {
      builder.ranks(ranks);
    }
    if (!StringUtils.isBlank(serverGrp)) {
      builder.serverGroup(serverGrp);
    }
    if (!StringUtils.isBlank(poolFlags)) {
      builder.poolFlags(Integer.valueOf(poolFlags));
    }

    DaosUns uns = builder.build();
    try {
      uns.destroyPath();
      log.info("UNS path destroyed.");
    } catch (Exception e) {
      log.error("failed to destroy UNS path.", e);
    }
  }

  private static void create() {
    String path = System.getProperty("path");
    String poolId = System.getProperty("pool_id");
    String contId = System.getProperty("cont_id");
    String layout = System.getProperty("layout");
    String objectType = System.getProperty("object_type");
    String chunkSize = System.getProperty("chunk_size");
    String onLustre = System.getProperty("on_lustre");
    String ranks = System.getProperty("ranks");
    String serverGrp = System.getProperty("server_group");
    String poolFlags = System.getProperty("pool_flags");
    String properties = System.getProperty("properties");

    if (StringUtils.isBlank(path)) {
      throw new IllegalArgumentException("need path, -Dpath=");
    }
    if (StringUtils.isBlank(poolId)) {
      throw new IllegalArgumentException("need pool UUID, -Dpool_id");
    }

    File file = new File(path);
    DaosUns.DaosUnsBuilder builder = new DaosUns.DaosUnsBuilder();
    builder.path(file.getAbsolutePath());
    builder.poolId(poolId);
    if (!StringUtils.isBlank(contId)) {
      builder.containerId(contId);
    }
    if (!StringUtils.isBlank(layout)) {
      builder.layout(Layout.valueOf(layout));
    }
    if (!StringUtils.isBlank(objectType)) {
      builder.objectType(DaosObjectType.valueOf(objectType));
    }
    if (!StringUtils.isBlank(chunkSize)) {
      builder.chunkSize(Long.valueOf(chunkSize));
    }
    if (!StringUtils.isBlank(onLustre)) {
      builder.onLustre(Boolean.valueOf(onLustre));
    }
    if (!StringUtils.isBlank(ranks)) {
      builder.ranks(ranks);
    }
    if (!StringUtils.isBlank(serverGrp)) {
      builder.serverGroup(serverGrp);
    }
    if (!StringUtils.isBlank(poolFlags)) {
      builder.poolFlags(Integer.valueOf(poolFlags));
    }
    if (!StringUtils.isBlank(properties)) {
      Properties.Builder pb = Properties.newBuilder();
      try {
        TextFormat.getParser().merge(properties, pb);
        Properties p = pb.build();
        builder.propReserved(p.getReserved());
        if (p.getEntriesCount() > 0) {
          for (Entry e : p.getEntriesList()) {
            Object o;
            switch (e.getValueCase()) {
              case STR:
                o = e.getStr();
                break;
              case VAL:
                o = e.getVal();
                break;
              case PVAL:
                o = e.getPval();
                break;
              default:
                throw new IllegalArgumentException("unknown value case: " + e.getValueCase());
            }
            builder.putEntry(e.getType(), new PropValue(o, e.getReserved()));
          }
        }
      } catch (Exception e) {
        log.error("failed to parse properties, " + properties, e);
        return;
      }
    }

    DaosUns uns = builder.build();
    try {
      uns.createPath();
      log.info("UNS path created.");
    } catch (Exception e) {
      log.error("failed to create UNS path.", e);
    }
  }

  private static boolean needUsage(String[] args) {
    for (String arg : args) {
      if ("--help".equals(arg) || "-h".equals(arg)) {
        return true;
      }
    }
    return false;
  }

  private static String getUsage() {
    String usage = "===================================================\n" +
        "create/resolve/destroy/parse UNS path associated with DAOS.\n" +
        "Usage java [-options] <-jar jarfile | -cp classpath> io.daos.dfs.DaosUns <command>\n" +
        "see following commands and their options:\n" +
        "command: [create|resolve|destroy|parse|setappinfo|getappinfo|util]\n" +
        "=>create:\n" +
        "   -Dpath=, required, OS file path. The file should not exist.\n" +
        "   -Dpool_id=, required, DAOS pool UUID.\n" +
        "   -Dcont_id=, optional, DAOS container UUID. New container will be created if not set.\n" +
        "   -Dlayout=, optional, filesystem layout. Default is POSIX. [POSIX|HDF5].\n" +
        "   -Dobject_type=, optional, file object type. Default is OC_SX. [enums from DaosObjectType]. " +
        "use the \"util -Dop=list-object-types\" command to list all object types.\n" +
        "   -Dchunk_size=, optional, file chunk size. Default is 0 which lets DAOS decides (1MB for now).\n" +
        "   -Don_lustre=, optional, on lustre file system? Default is false. [true|false]\n" +
        "   -Dranks=, optional, pool ranks. Default is 0.\n" +
        "   -Dserver_group=, optional, DAOS server group. Default is daos_server.\n" +
        "   -Dpool_flags=, optional, pool access flags. Default is 2. [1(readonly)|2(readwrite)|4(execute)].\n" +
        "   -Dproperties=, optional, properties. No default. " +
        "[key(enums from PropType)=value(value type can be get from PropValue.getValueClass(type))].\n" +
        "use the \"util -Dop=list-property-types\" command to list all property types.\n" +
        "use the \"util -Dop=get-property-value-type\" command to get property value type of giving property " +
        "type.\n" +
        "use the \"util -Dop=sample-properties\" command to see examples of setting different types " +
        "of properties.\n" +
        "=>resolve:\n" +
        "   -Dpath=, required, OS file path. The file should exist.\n" +
        "=>destroy:\n" +
        "   -Dpath=, required, OS file path. The file should exist.\n" +
        "   -Dpool_id=, required, DAOS pool UUID.\n" +
        "   -Dranks=, optional, pool ranks. Default is 0.\n" +
        "   -Dserver_group=, optional, DAOS server group. Default is daos_server.\n" +
        "   -Dpool_flags=, optional, pool access flags. Default is 2. [2(readwrite)].\n" +
        "=>parse:\n" +
        "   -Dinput=, required, attribute string.\n" +
        "=>setappinfo:\n" +
        "   -Dpath=, required, OS file path. The file should exist.\n" +
        "   -Dattr=, required, attribute name.\n" +
        "   -Dvalue=, optional, attribute value. The attribute will be removed if value is not specified.\n" +
        "=>getappinfo:\n" +
        "   -Dpath=, required, OS file path. The file should exist.\n" +
        "   -Dattr=, required, attribute name.\n" +
        "   -Dmaxlen=, optional, maximum length of value to be get. Default is 1024\n" +
        "=>util\n" +
        "   -Dop=, required, operation types. [list-object-types|list-property-types|" +
        "get-property-value-type|sample-properties|escape-app-value]\n" +
        "   -Dprop_type=, required when op=get-property-value-type\n" +
        "   -Dinput=, required when op=escape-app-value\n" +
        "===================================================\n" +
        "examples: java -Dpath=/tmp/uns -Dpool_id=<your pool uuid> -cp ./daos-java-1.1.0-shaded.jar " +
        "io.daos.dfs.DaosUns create\n" +
        "===================================================";
    return usage;
  }
}
