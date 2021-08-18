/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.dfs;

import java.io.IOException;

import com.google.protobuf.TextFormat;

import io.daos.*;
import io.daos.dfs.uns.*;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * A wrapper class of DAOS Unified Namespace. There are four DAOS UNS methods,
 * {@link #resolvePath(String)} and {@link #parseAttribute(String)}, wrapped in this class.
 *
 * <p>
 * Due to complexity of DAOS UNS attribute, duns_attr_t, protobuf and c plugin, protobuf-c, are introduced to
 * pass parameters accurately and efficiently. check DunsAttribute.proto and its auto-generated classes under
 * package io.daos.dfs.uns.
 *
 * <p>
 * The typical usage is to resolve path
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
    if (DaosUtils.isBlankStr(attrName)) {
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
    if (DaosUtils.isBlankStr(attrName)) {
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

  /**
   * A builder class to build {@link DaosUns} instance. Most of methods are same as ones
   * in {@link io.daos.dfs.DaosFsClient.DaosFsClientBuilder}, like
   * {@link #serverGroup(String)}, {@link #poolFlags(int)}.
   *
   */
  public static class DaosUnsBuilder implements Cloneable {
    private String path;
    private String poolUuid;
    private String contUuid;
    private Layout layout = Layout.POSIX;
    private DaosObjectType objectType = DaosObjectType.OC_SX;
    private long chunkSize = Constants.FILE_DEFAULT_CHUNK_SIZE;
    private boolean onLustre;
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
      try {
        duns.builder = clone();
      } catch (CloneNotSupportedException e) {
        throw new IllegalStateException("clone not supported.", e);
      }
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
      duns.attribute = builder.build();
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
      case "resolve":
        resolve();
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
    if (DaosUtils.isBlankStr(op)) {
      throw new IllegalArgumentException("need operation type.\n" + getUsage());
    }
    switch (op) {
      case "list-object-types":
        listObjectTypes();
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

  private static void escapeAppValue() {
    String input = System.getProperty("input");
    if (DaosUtils.isBlankStr(input)) {
      throw new IllegalArgumentException("need input.\n" + getUsage());
    }
    log.info("escaped value is: " + DaosUtils.escapeUnsValue(input));
  }

  private static String escapeDoubleQuote(String str) {
    return str.replaceAll("\"", "\\\\\"");
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
        "resolve/parse UNS path associated with DAOS.\n" +
        "Usage java [-options] <-jar jarfile | -cp classpath> io.daos.dfs.DaosUns <command>\n" +
        "see following commands and their options:\n" +
        "command: [resolve|parse|setappinfo|getappinfo|util]\n" +
        "=>resolve:\n" +
        "   -Dpath=, required, OS file path. The file should exist.\n" +
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
        "   -Dop=, required, operation types. [list-object-types|escape-app-value]\n" +
        "   -Dinput=, required when op=escape-app-value\n" +
        "===================================================\n" +
        "examples: java -Dpath=/tmp/uns io.daos.dfs.DaosUns resolve\n" +
        "===================================================";
    return usage;
  }
}
