/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.dfs;

import java.io.File;
import java.io.IOException;
import java.net.URI;

import io.daos.*;
import io.daos.dfs.uns.*;

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
   * The Rest gets from app extended attributes. A exception will be thrown if user expect app info and no
   * info gets.
   *
   * @param uri             URI starts with daos://
   * @return information hold in {@link DunsInfo}
   * @throws IOException
   * {@link DaosIOException}
   */
  public static DunsInfo getAccessInfo(URI uri) throws IOException {
    String fullPath = uri.toString();
    String path = fullPath;
    boolean direct = true;
    if (uri.getAuthority() == null || uri.getAuthority().startsWith(Constants.UNS_ID_PREFIX)) {
      path = uri.getPath();
      // make sure path exists
      File f = new File(path);
      while (f != null && !f.exists()) {
        f = f.getParentFile();
      }
      if (f == null) {
        return null;
      }
      path = f.getAbsolutePath();
      direct = false;
    }
    DunsAttribute attribute = DaosUns.resolvePath(path);
    if (attribute == null) {
      throw new IOException("no UNS attribute get from " + fullPath);
    }
    String poolId = attribute.getPoolId();
    String contId = attribute.getContId();
    Layout layout = attribute.getLayoutType();
    String relPath = attribute.getRelPath();
    String prefix;
    // is direct path ?
    if (direct) {
      if (!uri.getAuthority().equals(poolId)) {
        throw new IOException("authority " + uri.getAuthority() + ", should be equal to poolId: " + poolId);
      }
      prefix = "/" + contId;
      if (layout == Layout.UNKNOWN) {
        layout = Layout.POSIX; // default to posix
      }
    } else {
      int idx = path.indexOf(relPath);
      if (idx < 0) {
        throw new IOException("path: " + path + ", should contain real path: " + relPath);
      }
      prefix = idx > 0 ? path.substring(0, idx) : path;
    }
    return new DunsInfo(poolId, contId, layout.name(), prefix);
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

  public DaosObjectClass getObjectType() {
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
    private DaosObjectClass objectType = DaosObjectClass.OC_SX;
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
    public DaosUnsBuilder objectType(DaosObjectClass objectType) {
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
      builder.setPoolId(poolUuid);
      if (contUuid != null) {
        builder.setContId(contUuid);
      }
      builder.setLayoutType(layout);
      builder.setObjectType(objectType.nameWithoutOc());
      builder.setChunkSize(chunkSize);
      builder.setOnLustre(onLustre);
      duns.attribute = builder.build();
    }
  }
}
