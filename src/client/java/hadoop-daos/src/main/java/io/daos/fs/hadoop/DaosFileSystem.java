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

package io.daos.fs.hadoop;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.net.URI;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import com.google.common.collect.Lists;

import io.daos.*;
import io.daos.dfs.*;

import org.apache.commons.lang.StringEscapeUtils;
import org.apache.commons.lang.StringUtils;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.*;
import org.apache.hadoop.fs.permission.FsPermission;
import org.apache.hadoop.util.Progressable;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Implementation of {@link FileSystem} for DAOS file system.
 *
 * <p>
 *
 * <p>
 * Before instantiating this class, we need to do some configuration visible to
 * Hadoop. They are configured in daos-site.xml. See below table for
 * all mandatory and optional configuration items.
 *
 * <table>
 * <thead>
 *   <tr>
 *   <td>Item</td>
 *   <td>Default</td>
 *   <td>Range</td>
 *   <td>mandatory</td>
 *   <td>Description</td>
 *   </tr>
 * </thead>
 * <tbody>
 * <tr>
 *   <td>{@value io.daos.fs.hadoop.Constants#DAOS_SERVER_GROUP}</td>
 *   <td>{@value io.daos.Constants#POOL_DEFAULT_SERVER_GROUP}</td>
 *   <td></td>
 *   <td>false</td>
 *   <td>daos server group name</td>
 * </tr>
 * <tr>
 *   <td>{@value io.daos.fs.hadoop.Constants#DAOS_POOL_UUID}</td>
 *   <td></td>
 *   <td></td>
 *   <td>true</td>
 *   <td>UUID of DAOS pool</td>
 * </tr>
 * <tr>
 *   <td>{@value io.daos.fs.hadoop.Constants#DAOS_POOL_FLAGS}</td>
 *   <td>{@value io.daos.Constants#ACCESS_FLAG_POOL_READWRITE}</td>
 *   <td>
 *       {@value io.daos.Constants#ACCESS_FLAG_POOL_READONLY},
 *       {@value io.daos.Constants#ACCESS_FLAG_POOL_READWRITE},
 *       {@value io.daos.Constants#ACCESS_FLAG_POOL_EXECUTE}
 *   </td>
 *   <td>false</td>
 *   <td>pool access flags</td>
 * </tr>
 * <tr>
 *   <td>{@value io.daos.fs.hadoop.Constants#DAOS_CONTAINER_UUID}</td>
 *   <td></td>
 *   <td></td>
 *   <td>true</td>
 *   <td>UUID of DAOS container which created with "--type posix"</td>
 * </tr>
 * <tr>
 * <td>{@value io.daos.fs.hadoop.Constants#DAOS_READ_BUFFER_SIZE}</td>
 * <td>{@value io.daos.fs.hadoop.Constants#DEFAULT_DAOS_READ_BUFFER_SIZE}</td>
 * <td>{@value io.daos.fs.hadoop.Constants#MINIMUM_DAOS_READ_BUFFER_SIZE} -
 * {@value io.daos.fs.hadoop.Constants#MAXIMUM_DAOS_READ_BUFFER_SIZE}</td>
 * <td>false</td>
 * <td>size of direct buffer for reading data from DAOS</td>
 * </tr>
 * <tr>
 * <td>{@value io.daos.fs.hadoop.Constants#DAOS_WRITE_BUFFER_SIZE}</td>
 * <td>{@value io.daos.fs.hadoop.Constants#DEFAULT_DAOS_WRITE_BUFFER_SIZE}</td>
 * <td>{@value io.daos.fs.hadoop.Constants#MINIMUM_DAOS_WRITE_BUFFER_SIZE} -
 * {@value io.daos.fs.hadoop.Constants#MAXIMUM_DAOS_WRITE_BUFFER_SIZE}</td>
 * <td>false</td>
 * <td>size of direct buffer for writing data to DAOS</td>
 * </tr>
 * <tr>
 * <td>{@value io.daos.fs.hadoop.Constants#DAOS_BLOCK_SIZE}</td>
 * <td>{@value io.daos.fs.hadoop.Constants#DEFAULT_DAOS_BLOCK_SIZE}</td>
 * <td>{@value io.daos.fs.hadoop.Constants#MINIMUM_DAOS_BLOCK_SIZE} -
 * {@value io.daos.fs.hadoop.Constants#MAXIMUM_DAOS_BLOCK_SIZE}</td>
 * <td>false</td>
 * <td>size for splitting large file into blocks when read by Hadoop</td>
 * </tr>
 * <tr>
 * <td>{@value io.daos.fs.hadoop.Constants#DAOS_CHUNK_SIZE}</td>
 * <td>{@value io.daos.fs.hadoop.Constants#DEFAULT_DAOS_CHUNK_SIZE}</td>
 * <td>{@value io.daos.fs.hadoop.Constants#MINIMUM_DAOS_CHUNK_SIZE} -
 * {@value io.daos.fs.hadoop.Constants#MAXIMUM_DAOS_CHUNK_SIZE}</td>
 * <td>false</td>
 * <td>size of DAOS file chunk</td>
 * </tr>
 * <tr>
 * <td>{@value io.daos.fs.hadoop.Constants#DAOS_READ_MINIMUM_SIZE}</td>
 * <td>{@value io.daos.fs.hadoop.Constants#MINIMUM_DAOS_READ_BUFFER_SIZE}</td>
 * <td>{@value io.daos.fs.hadoop.Constants#MINIMUM_DAOS_READ_BUFFER_SIZE} -
 * {@value io.daos.fs.hadoop.Constants#MAXIMUM_DAOS_READ_BUFFER_SIZE}</td>
 * <td>false</td>
 * <td>size of DAOS file chunk</td>
 * </tr>
 * </tbody>
 * </table>
 *
 * <pre>
 * User can use below statement to make their configuration visible to Hadoop.
 * <code>
 * Configuration cfg = new Configuration();
 * cfg.addResource("path to your configuration file");
 * </code>
 * </pre>
 *
 * <p>
 * To get instance of this class via Hadoop FileSystem, please refer to the package description.
 */
public class DaosFileSystem extends FileSystem {
  private static final Logger LOG = LoggerFactory.getLogger(DaosFileSystem.class);
  private Path workingDir;
  private URI uri;
  private DaosFsClient daos;
  private int readBufferSize;
  private int writeBufferSize;
  private int blockSize;
  private int chunkSize;
  private int minReadSize;
  private String bucket;
  private boolean uns;
  private String unsPrefix;
  private String qualifiedUnsPrefix;
  private String qualifiedUnsWorkPath;
  private String workPath;

  static {
    if (ShutdownHookManager.removeHook(DaosClient.FINALIZER)) {
      org.apache.hadoop.util.ShutdownHookManager.get().addShutdownHook(DaosClient.FINALIZER, 0);
      if (LOG.isDebugEnabled()) {
        LOG.debug("daos finalizer relocated to hadoop ShutdownHookManager");
      }
    } else {
      LOG.error("failed to relocate daos finalizer");
    }
  }

  @Override
  public void initialize(URI name, Configuration conf)
          throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosFileSystem initializing for " + name);
    }
    if (!getScheme().equals(name.getScheme())) {
      throw new IllegalArgumentException("schema should be " + getScheme());
    }
    DunsInfo info = searchUnsPath(name.getPath());
    if (info != null) {
      LOG.info("initializing from uns path, " + name);
      uns = true;
      unsPrefix = info.getPrefix();
      initializeFromUns(name, conf, info);
    } else {
      LOG.info("initializing from config file, " + name);
      uns = false;
      initializeFromConfigFile(name, conf);
    }
  }

  /**
   * initialize from DAOS UNS.
   * Existing configuration from <code>conf</code> will be overwritten by UNS configs if any.
   *
   * @param name
   * hadoop URI
   * @param conf
   * hadoop configuration
   * @param unsInfo
   * information get from UNS path
   * @throws IOException
   * {@link DaosIOException}
   */
  private void initializeFromUns(URI name, Configuration conf, DunsInfo unsInfo) throws IOException {
    String path = name.getPath();
    if (!path.startsWith("/")) {
      throw new IllegalArgumentException("path should be started with /, " + path);
    }

    Set<String> exProps = new HashSet<>();
    exProps.add(Constants.DAOS_POOL_UUID);
    exProps.add(Constants.DAOS_CONTAINER_UUID);
    DaosConfigFile.getInstance().merge(name.getAuthority(), conf, exProps);
    parseUnsConfig(conf, unsInfo);
    super.initialize(name, conf);
    validateAndConnect(name, conf);
  }

  /**
   * search UNS path from given <code>path</code> or its ancestors.
   *
   * @param path
   * path of URI
   * @return DunsInfo
   * @throws IOException
   * {@link DaosIOException}
   */
  private DunsInfo searchUnsPath(String path) throws IOException {
    if ("/".equals(path) || !path.startsWith("/")) {
      return null;
    }
    File file = new File(path);
    DunsInfo info = null;
    while (info == null && file != null) {
      try {
        info = DaosUns.getAccessInfo(file.getAbsolutePath(), Constants.UNS_ATTR_NAME_HADOOP,
          io.daos.Constants.UNS_ATTR_VALUE_MAX_LEN_DEFAULT, false);
        if (info != null) {
          break;
        }
      } catch (DaosIOException e) {
        // ignoring error
      }
      file = file.getParentFile();
    }
    return info;
  }

  private void parseUnsConfig(Configuration conf, DunsInfo info) throws IOException {
    if (!"POSIX".equalsIgnoreCase(info.getLayout())) {
      throw new IllegalArgumentException("expect POSIX file system, but " + info.getLayout());
    }
    String poolId = info.getPoolId();
    String contId = info.getContId();
    String appInfo = info.getAppInfo();
    if (appInfo != null) {
      String[] pairs = appInfo.split(":");
      for (String pair : pairs) {
        if (StringUtils.isBlank(pair)) {
          continue;
        }
        String[] kv = pair.split("=");
        try {
          switch (kv[0]) {
            case Constants.DAOS_SERVER_GROUP:
              if (StringUtils.isBlank(conf.get(Constants.DAOS_SERVER_GROUP))) {
                conf.set(Constants.DAOS_SERVER_GROUP, StringEscapeUtils.unescapeJava(kv[1]));
              }
              break;
            case Constants.DAOS_POOL_UUID:
              if (StringUtils.isBlank(poolId)) {
                poolId = StringEscapeUtils.unescapeJava(kv[1]);
              } else {
                LOG.warn("ignoring pool id {} from app info", kv[1]);
              }
              break;
            case Constants.DAOS_POOL_SVC:
              if (StringUtils.isBlank(conf.get(Constants.DAOS_POOL_SVC))) {
                conf.set(Constants.DAOS_POOL_SVC, StringEscapeUtils.unescapeJava(kv[1]));
              }
              break;
            case Constants.DAOS_POOL_FLAGS:
              if (StringUtils.isBlank(conf.get(Constants.DAOS_POOL_FLAGS))) {
                conf.set(Constants.DAOS_POOL_FLAGS, StringEscapeUtils.unescapeJava(kv[1]));
              }
              break;
            case Constants.DAOS_CONTAINER_UUID:
              if (StringUtils.isBlank(contId)) {
                contId = StringEscapeUtils.unescapeJava(kv[1]);
              } else {
                LOG.warn("ignoring container id {} from app info", kv[1]);
              }
              break;
            case Constants.DAOS_READ_BUFFER_SIZE:
            case Constants.DAOS_WRITE_BUFFER_SIZE:
            case Constants.DAOS_BLOCK_SIZE:
            case Constants.DAOS_CHUNK_SIZE:
            case Constants.DAOS_READ_MINIMUM_SIZE:
              if (StringUtils.isBlank(conf.get(kv[0]))) {
                conf.setInt(kv[0], Integer.valueOf(kv[1]));
              }
              break;
            default:
              throw new IllegalArgumentException("unknown daos config, " + kv[0]);
          }
        } catch (NumberFormatException e) {
          throw new IllegalArgumentException("bad config " + pair, e);
        }
      }
    }
    // TODO: other info, like svc, will be moved to agent. then change accordingly.
    conf.set(Constants.DAOS_POOL_UUID, poolId);
    conf.set(Constants.DAOS_CONTAINER_UUID, contId);
  }

  /**
   * initialize from daos-site.xml.
   *
   * @param name
   * hadoop URI
   * @param conf
   * hadoop configuration
   * @throws IOException
   * {@link DaosIOException}
   */
  private void initializeFromConfigFile(URI name, Configuration conf) throws IOException {
    conf = DaosConfigFile.getInstance().parseConfig(name.getAuthority(), conf);
    super.initialize(name, conf);

    validateAndConnect(name, conf);
  }

  private void validateAndConnect(URI name, Configuration conf) throws IOException {
    try {
      this.bucket = name.getHost();
      this.readBufferSize = conf.getInt(Constants.DAOS_READ_BUFFER_SIZE, Constants.DEFAULT_DAOS_READ_BUFFER_SIZE);
      this.writeBufferSize = conf.getInt(Constants.DAOS_WRITE_BUFFER_SIZE, Constants.DEFAULT_DAOS_WRITE_BUFFER_SIZE);
      this.blockSize = conf.getInt(Constants.DAOS_BLOCK_SIZE, Constants.DEFAULT_DAOS_BLOCK_SIZE);
      this.chunkSize = conf.getInt(Constants.DAOS_CHUNK_SIZE, Constants.DEFAULT_DAOS_CHUNK_SIZE);
      this.minReadSize = conf.getInt(Constants.DAOS_READ_MINIMUM_SIZE, Constants.MINIMUM_DAOS_READ_BUFFER_SIZE);
      if (minReadSize > readBufferSize || minReadSize <= 0) {
        LOG.warn("overriding minReadSize to readBufferSize " + readBufferSize);
        minReadSize = readBufferSize;
      }

      checkSizeMin(readBufferSize, Constants.MINIMUM_DAOS_READ_BUFFER_SIZE,
              "internal read buffer size should be no less than ");
      checkSizeMin(writeBufferSize, Constants.MINIMUM_DAOS_WRITE_BUFFER_SIZE,
              "internal write buffer size should be no less than ");
      checkSizeMin(blockSize, Constants.MINIMUM_DAOS_BLOCK_SIZE,
              "block size should be no less than ");
      checkSizeMin(chunkSize, Constants.MINIMUM_DAOS_CHUNK_SIZE,
              "daos chunk size should be no less than ");

      checkSizeMax(readBufferSize, Constants.MAXIMUM_DAOS_READ_BUFFER_SIZE,
              "internal read buffer size should not be greater than ");
      checkSizeMax(writeBufferSize, Constants.MAXIMUM_DAOS_WRITE_BUFFER_SIZE,
              "internal write buffer size should not be greater than ");
      checkSizeMax(blockSize, Constants.MAXIMUM_DAOS_BLOCK_SIZE, "block size should be not be greater than ");
      checkSizeMax(chunkSize, Constants.MAXIMUM_DAOS_CHUNK_SIZE, "daos chunk size should not be greater than ");

      String svrGrp = conf.get(Constants.DAOS_SERVER_GROUP);
      String poolFlags = conf.get(Constants.DAOS_POOL_FLAGS);
      String svc = conf.get(Constants.DAOS_POOL_SVC);

      String poolUuid = conf.get(Constants.DAOS_POOL_UUID);
      if (StringUtils.isEmpty(poolUuid)) {
        throw new IllegalArgumentException(Constants.DAOS_POOL_UUID +
                " is null , need to set " + Constants.DAOS_POOL_UUID);
      }
      String contUuid = conf.get(Constants.DAOS_CONTAINER_UUID);
      if (StringUtils.isEmpty(contUuid)) {
        throw new IllegalArgumentException(Constants.DAOS_CONTAINER_UUID +
                " is null, need to set " + Constants.DAOS_CONTAINER_UUID);
      }

      if (LOG.isDebugEnabled()) {
        LOG.debug(name + " configs:");
        if (!StringUtils.isBlank(svrGrp)) {
          LOG.debug("daos server group: " + svrGrp);
        }
        LOG.debug("pool uuid: " + poolUuid);
        if (!StringUtils.isBlank(poolFlags)) {
          LOG.debug("pool flags: " + poolFlags);
        }
        LOG.debug("container uuid: " + contUuid);
        if (!StringUtils.isBlank(svc)) {
          LOG.debug("pool svc: " + svc);
        }
        LOG.debug("read buffer size " + readBufferSize);
        LOG.debug("write buffer size: " + writeBufferSize);
        LOG.debug("block size: " + blockSize);
        LOG.debug("chunk size: " + chunkSize);
        LOG.debug("min read size: " + minReadSize);
      }

      // daosFSclient build
      DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder().poolId(poolUuid)
              .containerId(contUuid);
      if (!StringUtils.isBlank(svrGrp)) {
        builder.serverGroup(svrGrp);
      }
      if (!StringUtils.isBlank(poolFlags)) {
        builder.poolFlags(Integer.valueOf(poolFlags));
      }
      if (!StringUtils.isBlank(svc)) {
        builder.ranks(svc);
      }
      this.daos = builder.build();
      String tmpUri = name.getScheme() + "://" + (name.getAuthority() == null ? "/" : name.getAuthority());
      workPath = "/user/" + System.getProperty("user.name");
      this.uri = URI.create(tmpUri);
      if (uns) {
        qualifiedUnsPrefix = tmpUri + unsPrefix;
        qualifiedUnsWorkPath = qualifiedUnsPrefix + workPath;
        workingDir = new Path(qualifiedUnsWorkPath);
      } else {
        this.workingDir = new Path(workPath)
          .makeQualified(this.uri, this.getWorkingDirectory());
      }
      // mkdir workingDir in DAOS
      daos.mkdir(workPath, true);
      setConf(conf);
      LOG.info("DaosFileSystem initialized");
    } catch (IOException e) {
      throw new IOException("failed to initialize " + this.getClass().getName(), e);
    }
  }

  public boolean isUns() {
    return uns;
  }

  public String getUnsPrefix() {
    return unsPrefix;
  }

  @Override
  public int getDefaultPort() {
    return 1;
  }

  private void checkSizeMin(int size, int min, String msg) {
    if (size < min) {
      throw new IllegalArgumentException(msg + min + ", size is " + size);
    }
  }

  private void checkSizeMax(int size, long max, String msg) {
    if (size > max) {
      throw new IllegalArgumentException(msg + max + ", size is " + size);
    }
  }

  /**
   * Return the protocol scheme for the FileSystem.
   *
   * @return {@link Constants#DAOS_SCHEMA}
   */
  @Override
  public String getScheme() {
    return Constants.DAOS_SCHEMA;
  }

  @Override
  public URI getUri() {
    return uri;
  }

  /**
   * This method make sure schema and authority are prepended to path.
   *
   * @param p
   * path to resolve
   * @return path with schema and authority
   */
  @Override
  public Path resolvePath(final Path p) {
    if (!uns) {
      return p.makeQualified(getUri(), this.getWorkingDirectory());
    }
    // UNS path
    URI puri = p.toUri();
    if (puri.getScheme() == null && puri.getAuthority() == null) {
      String path = puri.getPath();
      if (!path.startsWith(unsPrefix)) {
        path = path.startsWith("/") ? (qualifiedUnsPrefix + path) :
          (qualifiedUnsWorkPath + "/" + path);
      } else {
        path = qualifiedUnsPrefix.substring(0, qualifiedUnsPrefix.indexOf(unsPrefix)) + path;
      }
      return new Path(path);
    }
    return p;
  }

  private String getDaosRelativePath(Path path) {
    String p = path.toUri().getPath();
    boolean truncated = false;
    if (uns && p.startsWith(unsPrefix)) {
      if (p.length() > unsPrefix.length()) {
        p = p.substring(unsPrefix.length());
        truncated = true;
      } else {
        p = "/";
      }
    }
    if (!p.startsWith("/")) {
      if (truncated) { // ensure correct uns prefix, counter example, <unsPrefix>abc, is not on uns path
        return path.toUri().getPath();
      }
      p = workPath + "/" + p;
    }
    return p;
  }

  @Override
  public FSDataInputStream open(
          Path f,
          final int bufferSize) throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosFileSystem open :  path = " + f.toUri().getPath() + " ; buffer size = " + bufferSize);
    }

    String p = getDaosRelativePath(f);
    DaosFile file = daos.getFile(p);
    if (!file.exists()) {
      throw new FileNotFoundException(f + " not exist");
    }

    if (file.isDirectory()) {
      throw  new FileNotFoundException("can't open " + f + " because it is a directory ");
    }

    return new FSDataInputStream(new DaosInputStream(
            file, statistics, readBufferSize, bufferSize < minReadSize ? minReadSize : bufferSize));
  }

  @Override
  public FSDataInputStream open(Path f) throws IOException {
    return open(f, readBufferSize);
  }

  @Override
  public FSDataOutputStream create(Path f,
                                   FsPermission permission,
                                   boolean overwrite,
                                   int bufferSize,
                                   short replication,
                                   long bs,
                                   Progressable progress) throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosFileSystem create file , path= " + f.toUri().toString() + ", buffer size = " + bufferSize +
              ", block size = " + bs);
    }
    String key = getDaosRelativePath(f);

    DaosFile daosFile = this.daos.getFile(key);

    if (daosFile.exists()) {
      if (daosFile.isDirectory()) {
        throw new FileAlreadyExistsException(f + " is a directory ");
      }
      if (!overwrite) {
        throw new FileAlreadyExistsException(f + "already exists ");
      }
      if (!daosFile.delete()) {
        throw new IOException("failed to delete existing file " + daosFile);
      }
    }

    daosFile.createNewFile(
            Constants.DAOS_MODLE,
            DaosObjectType.OC_SX,
            this.chunkSize,
            true);

    return new FSDataOutputStream(new DaosOutputStream(daosFile, key, writeBufferSize, statistics), statistics);
  }

  @Override
  public FSDataOutputStream append(Path f,
                                   int bufferSize,
                                   Progressable progress) throws IOException {
    throw new IOException("Append is not supported");
  }

  /**
   * Renames Path src to Path dst. Can take place on remote DAOS.
   *
   * @param src path to be renamed
   * @param dst new path after rename
   * @return
   */
  @Override
  public boolean rename(Path src, Path dst) {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosFileSystem: rename old path {} to new path {}", src.toUri().getPath(), dst.toUri().getPath());
    }
    String srcPath = getDaosRelativePath(src);
    String destPath = getDaosRelativePath(dst);
    DaosFile srcDaosFile = this.daos.getFile(srcPath);
    DaosFile dstDaosFile = this.daos.getFile(destPath);

    try {
      return innerRename(srcDaosFile, dstDaosFile);
    } catch (FileNotFoundException e) {
      if (LOG.isDebugEnabled()) {
        LOG.debug(e.toString());
      }
      return false;
    } catch (IOException e) {
      if (LOG.isDebugEnabled()) {
        LOG.debug(e.getMessage());
      }
      return false;
    }
  }

  /**
   * The inner rename operation. See {@link #rename(Path, Path)} for
   * the description of the operation.
   * This operation throws an exception on any failure  which needs to be
   * reported and downgraded to a failure.
   *
   * @param srcDaosFile path to be renamed
   * @param dstDaosFile new path after rename
   * @return true for successful renaming. false otherwise
   * @throws IOException on IO failure
   */

  private boolean innerRename(DaosFile srcDaosFile, DaosFile dstDaosFile) throws IOException {
    // determine  if src is root dir and whether it exits
    Path src = new Path(srcDaosFile.getPath());
    Path dst = new Path(dstDaosFile.getPath());
    if (srcDaosFile.getPath().equals("/")) {
      if (LOG.isDebugEnabled()) {
        LOG.debug("DaosFileSystem: can not rename root path {}", src);
      }
      throw new IOException("cannot move root / directory");
    }

    if (dst.toUri().getPath().equals("/")) {
      if (LOG.isDebugEnabled()) {
        LOG.debug("DaosFileSystem: can not rename root path {}", dst);
      }
      throw new IOException("cannot move root / directory");
    }

    if (!srcDaosFile.exists()) {
      throw new FileNotFoundException(String.format(
              "Failed to rename %s to %s, src dir do not !", src ,dst));
    }

    if (!dstDaosFile.exists()) {
      // if dst not exists and rename src to dst
      innerMove(srcDaosFile, dst);
      return true;
    }

    if (srcDaosFile.isDirectory() && !dstDaosFile.isDirectory()) {
      // If dst exists and not a directory / not empty
      throw new FileAlreadyExistsException(String.format(
              "Failed to rename %s to %s, file already exists or not empty!",
              src, dst));
    } else if (srcDaosFile.getPath().equals(dstDaosFile.getPath())) {
      if (LOG.isDebugEnabled()) {
        LOG.debug("DaosFileSystem:  src and dst refer to the same file or directory ");
      }
      return !srcDaosFile.isDirectory();
    }
    if (dstDaosFile.isDirectory() &&
            srcDaosFile.isDirectory() &&
            dstDaosFile.getPath().contains(srcDaosFile.getPath())) {
      // If dst exists and not a directory / not empty
      throw new IOException(String.format(
                "Failed to rename %s to %s, source dir can't move to a subdirectory of itself!",
                src, dst));
    }
    if (dstDaosFile.isDirectory()) {
      // If dst is a directory
      dst = new Path(dst, src.getName());
    } else {
      // If dst is not a directory
      throw new FileAlreadyExistsException(String.format(
                "Failed to rename %s to %s, file already exists ,%s is not a directory!", src, dst ,dst));
    }
    innerMove(srcDaosFile, dst);
    return true;
  }

  /**
   * The DAOS move operation.
   * This operation translate and throws an exception on any failure which
   *
   * @param srcDaosFile path to be renamed
   * @param dst new path after rename
   * @throws IOException hadoop compatible exception
   */
  private void innerMove(DaosFile srcDaosFile, Path  dst) throws IOException {
    try {
      srcDaosFile.rename(dst.toUri().getPath());
    } catch (IOException ioexception) {
      if (ioexception instanceof DaosIOException ) {
        DaosIOException daosIOException = (DaosIOException) ioexception;
        throw HadoopDaosUtils.translateException(daosIOException);
      }
      throw ioexception;
    }
  }

  @Override
  public boolean delete(Path f, boolean recursive) throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosFileSystem:   delete  path = {} - recursive = {}", f.toUri().getPath(), recursive);
    }
    DaosFile file = daos.getFile(f.toUri().getPath());

    FileStatus[] statuses;

    // indicating root directory "/".
    if (f.toUri().getPath().equals("/")) {
      statuses = listStatus(f);
      boolean isEmptyDir = statuses.length <= 0;
      return rejectRootDirectoryDelete(isEmptyDir, recursive);
    }
    if (!file.exists()) {
      if (LOG.isDebugEnabled()) {
        LOG.debug(String.format(
                "Failed to delete %s , path do not exists!", f));
      }
      return false;
    }
    if (file.isDirectory()) {
      if (LOG.isDebugEnabled()) {
        LOG.debug("DaosFileSystem: Path is a directory");
      }
      if (recursive) {
        // delete the dir and all files in the dir
        return file.delete(recursive);
      } else {
        statuses = listStatus(f);
        if (statuses != null && statuses.length > 0) {
          throw new IOException("DaosFileSystem delete : There are files in dir ");
        } else if (statuses != null && statuses.length == 0) {
          // delete empty dir
          return file.delete(recursive);
        }
      }
    }
    return file.delete(recursive);
  }

  /**
   * Implements the specific logic to reject root directory deletion.
   * The caller must return the result of this call, rather than
   * attempt to continue with the delete operation: deleting root
   * directories is never allowed. This method simply implements
   * the policy of when to return an exit code versus raise an exception.
   *
   * @param isEmptyDir empty directory or not
   * @param recursive recursive flag from command
   * @return a return code for the operation
   * @throws PathIOException if the operation was explicitly rejected.
   */
  private boolean rejectRootDirectoryDelete(boolean isEmptyDir,
                                            boolean recursive) throws IOException {
    LOG.info("oss delete the {} root directory of {}",this.bucket ,recursive);
    if (isEmptyDir) {
      return true;
    }
    if (recursive) {
      return false;
    } else {
      // reject
      throw new PathIOException(bucket, "Cannot delete root path");
    }
  }

  @Override
  public FileStatus[] listStatus(Path f) throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosFileSystem listStatus :  List status for path = {}", f.toUri().getPath());
    }
    String path = getDaosRelativePath(f);
    DaosFile file = daos.getFile(path);
    final List<FileStatus> result = Lists.newArrayList();
    try {
      if (file.isDirectory()) {
        String[] children = file.listChildren();
        if (children != null && children.length > 0) {
          for (String child : children) {
            FileStatus childStatus = getFileStatus(resolvePath(new Path(path, child)),
                    daos.getFile(file, child));
            result.add(childStatus);
          }
        }
      } else {
        result.add(getFileStatus(resolvePath(new Path(path)), file));
      }
    } catch (DaosIOException e) {
      throw HadoopDaosUtils.translateException(e);
    }
    return result.toArray(new FileStatus[result.size()]);
  }

  @Override
  public void setWorkingDirectory(Path newdir) {
    workingDir = newdir;
  }

  @Override
  public Path getWorkingDirectory() {
    return workingDir;
  }

  @Override
  public boolean mkdirs(Path f, FsPermission permission) throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosFileSystem mkdirs: Making directory = {} ", f.toUri().getPath());
    }

    String key = getDaosRelativePath(f);
    DaosFile file = daos.getFile(key);
    if (file.exists()) {
      // if the thread reaches here, there is something at the path
      if (file.isDirectory()) {
        return true;
      } else {
        throw new FileAlreadyExistsException("Not a directory: " + f);
      }
    } else {
      try {
        file.mkdirs();
      } catch (IOException ioe) {
        if (ioe instanceof DaosIOException ) {
          DaosIOException daosIOException = (DaosIOException) ioe;
          throw HadoopDaosUtils.translateException(daosIOException);
        }
        throw ioe;
      }
    }
    return true;
  }

  /**
   * get DAOS file status with detailed info, like modification time, access time, names.
   *
   * @param f
   * file path
   * @return file status with times and username and groupname
   * @throws IOException hadoop compatible exception
   */
  @Override
  public FileStatus getFileStatus(Path f) throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosFileSystem getFileStatus:  Get File Status , path = {}", f.toUri().getPath());
    }
    String key = getDaosRelativePath(f);
    return getFileStatus(f, daos.getFile(key));
  }

  @Override
  protected void rename(Path src, Path dst, Options.Rename... options) throws IOException {
    super.rename(src, dst, options);
  }

  @Override
  public void moveFromLocalFile(Path[] srcs, Path dst) throws IOException {
    super.moveFromLocalFile(srcs, dst);
  }

  private FileStatus getFileStatus(Path path, DaosFile file) throws IOException {
    if (!file.exists()) {
      throw new FileNotFoundException(file + "doesn't exist");
    }
    StatAttributes attributes = file.getStatAttributes();
    return new FileStatus(attributes.getLength(),
            !attributes.isFile(),
            1,
            attributes.isFile() ? blockSize : 0,
            DaosUtils.toMilliSeconds(attributes.getModifyTime()),
            DaosUtils.toMilliSeconds(attributes.getAccessTime()),
            null,
            attributes.getUsername(),
            attributes.getGroupname(),
            path);
  }

  @Override
  public boolean exists(Path f) {
    if (LOG.isDebugEnabled()) {
      LOG.debug(" DaosFileSystem exists: Is path = {} exists", f.toUri().getPath());
    }
    try {
      String key = getDaosRelativePath(f);
      return daos.exists(key);
    } catch (IOException e) {
      return false;
    }
  }

  @Override
  public void close() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosFileSystem close");
    }
    super.close();
    if (daos != null) {
      daos.close();
    }
  }
}
