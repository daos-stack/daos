/*
 * (C) Copyright 2018-2019 Intel Corporation.
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

package com.intel.daos.hadoop.fs;

import java.io.FileNotFoundException;
import java.io.IOException;
import java.net.URI;
import java.util.List;

import com.google.common.collect.Lists;
import com.intel.daos.client.*;
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
 * Hadoop. See below table for all mandatory and optional configuration items.
 *
 * <table>
 * <th><td>Item</td><td>Default</td><td>Range</td><td>Mandatory</td><td>Description</td></th>
 * <tr>
 * <td>{@link Constants#DAOS_READ_BUFFER_SIZE}</td>
 * <td>{@link Constants#DEFAULT_DAOS_READ_BUFFER_SIZE}</td>
 * <td>{@link Constants#MINIMUM_DAOS_READ_BUFFER_SIZE} - {@link Constants#MAXIMUM_DAOS_READ_BUFFER_SIZE}</td>
 * <td>false</td>
 * <td>size of direct buffer for reading data from DAOS</td>
 * </tr>
 * <tr>
 * <td>{@link Constants#DAOS_WRITE_BUFFER_SIZE}</td>
 * <td>{@link Constants#DEFAULT_DAOS_WRITE_BUFFER_SIZE}</td>
 * <td>{@link Constants#MINIMUM_DAOS_WRITE_BUFFER_SIZE} - {@link Constants#MAXIMUM_DAOS_WRITE_BUFFER_SIZE}</td>
 * <td>false</td>
 * <td>size of direct buffer for writing data to DAOS</td>
 * </tr>
 * <tr>
 * <td>{@link Constants#DAOS_BLOCK_SIZE}</td>
 * <td>{@link Constants#DEFAULT_DAOS_BLOCK_SIZE}</td>
 * <td>{@link Constants#MINIMUM_DAOS_BLOCK_SIZE} - {@link Constants#MAXIMUM_DAOS_BLOCK_SIZE}</td>
 * <td>size for splitting large file into blocks when read by Hadoop</td>
 * </tr>
 * <tr>
 * <td>{@link Constants#DAOS_CHUNK_SIZE}</td>
 * <td>{@link Constants#DEFAULT_DAOS_CHUNK_SIZE}</td>
 * <td>{@link Constants#MINIMUM_DAOS_CHUNK_SIZE} - {@link Constants#MAXIMUM_DAOS_CHUNK_SIZE}</td>
 * <td>size of DAOS file chunk</td>
 * </tr>
 * <tr>
 * <td>{@link Constants#DAOS_PRELOAD_SIZE}</td>
 * <td>{@link Constants#DEFAULT_DAOS_PRELOAD_SIZE}</td>
 * <td>{@link Constants#MINIMUM_DAOS_PRELOAD_SIZE} - {@link Constants#MAXIMUM_DAOS_PRELOAD_SIZE}</td>
 * <td>size for pre-loading more than requested data from DAOS into internal buffer when read</td>
 * </tr>
 * <tr>
 * <td>{@link Constants#DAOS_BUFFERED_READ_ENABLED}</td>
 * <td>true</td>
 * <td>true or false</td>
 * <td>true for pre-loading more than requested data. false for loading exactly requested data</td>
 * </tr>
 * </table>
 *
 * <p>
 * User can use below statement to make their configuration visible to Hadoop.
 * <code>
 * Configuration cfg = new Configuration();
 * cfg.addResource("path to your configuration file");
 * </code>
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
  private int preLoadBufferSize;
  private boolean bufferedReadEnabled;
  private int writeBufferSize;
  private int blockSize;
  private int chunkSize;

  @Override
  public void initialize(URI name, Configuration conf)
          throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosFileSystem initialize");
    }
    if (!getScheme().equals(name.getScheme())) {
      throw new IllegalArgumentException("schema should be " + getScheme());
    }
    if (StringUtils.isEmpty(name.getAuthority())) {
      throw new IllegalArgumentException("authority (ip:port) cannot be empty. we need it to identify a " +
              "unique DAOS File System");
    }

    super.initialize(name, conf);

    try {

      this.readBufferSize = conf.getInt(Constants.DAOS_READ_BUFFER_SIZE, Constants.DEFAULT_DAOS_READ_BUFFER_SIZE);
      this.writeBufferSize = conf.getInt(Constants.DAOS_WRITE_BUFFER_SIZE, Constants.DEFAULT_DAOS_WRITE_BUFFER_SIZE);
      this.blockSize = conf.getInt(Constants.DAOS_BLOCK_SIZE, Constants.DEFAULT_DAOS_BLOCK_SIZE);
      this.chunkSize = conf.getInt(Constants.DAOS_CHUNK_SIZE, Constants.DEFAULT_DAOS_CHUNK_SIZE);
      this.preLoadBufferSize = conf.getInt(Constants.DAOS_PRELOAD_SIZE, Constants.DEFAULT_DAOS_PRELOAD_SIZE);
      this.bufferedReadEnabled = conf.getBoolean(Constants.DAOS_BUFFERED_READ_ENABLED,
              Constants.DEFAULT_BUFFERED_READ_ENABLED);

      checkSizeMin(readBufferSize, Constants.MINIMUM_DAOS_READ_BUFFER_SIZE,
              "internal read buffer size should be no less than ");
      checkSizeMin(writeBufferSize, Constants.MINIMUM_DAOS_WRITE_BUFFER_SIZE,
              "internal write buffer size should be no less than ");
      checkSizeMin(blockSize, Constants.MINIMUM_DAOS_BLOCK_SIZE,
              "block size should be no less than ");
      checkSizeMin(chunkSize, Constants.MINIMUM_DAOS_CHUNK_SIZE,
              "daos chunk size should be no less than ");
      checkSizeMin(preLoadBufferSize, Constants.MINIMUM_DAOS_PRELOAD_SIZE,
              "preload buffer size should be no less than ");

      checkSizeMax(readBufferSize, Constants.MAXIMUM_DAOS_READ_BUFFER_SIZE,
              "internal read buffer size should not be greater than ");
      checkSizeMax(writeBufferSize, Constants.MAXIMUM_DAOS_WRITE_BUFFER_SIZE,
              "internal write buffer size should not be greater than ");
      checkSizeMax(blockSize, Constants.MAXIMUM_DAOS_BLOCK_SIZE, "block size should be not be greater than ");
      checkSizeMax(chunkSize, Constants.MAXIMUM_DAOS_CHUNK_SIZE, "daos chunk size should not be greater than ");
      checkSizeMax(preLoadBufferSize, Constants.MAXIMUM_DAOS_PRELOAD_SIZE,
              "preload buffer size should not be greater than ");

      if (preLoadBufferSize > readBufferSize) {
        throw new IllegalArgumentException("preload buffer size " + preLoadBufferSize +
                " should not be greater than reader buffer size, " + readBufferSize);
      }

      String poolUuid = conf.get(Constants.DAOS_POOL_UUID);
      if (StringUtils.isEmpty(poolUuid)) {
        throw new IllegalArgumentException(Constants.DAOS_POOL_UUID +
                " is null , need to set up " + Constants.DAOS_POOL_UUID + " in hdfs.xml .");
      }
      String contUuid = conf.get(Constants.DAOS_CONTAINER_UUID);
      if (StringUtils.isEmpty(contUuid)) {
        throw new IllegalArgumentException(Constants.DAOS_CONTAINER_UUID +
                " is null, need to set up " + Constants.DAOS_CONTAINER_UUID + " in hdfs.xml .");
      }
      String svc = conf.get(Constants.DAOS_POOL_SVC);
      if (StringUtils.isEmpty(svc)) {
        throw new IllegalArgumentException(Constants.DAOS_POOL_SVC +
                " is null, need to set up " + Constants.DAOS_POOL_SVC + " in hdfs.xml .");
      }

      // daosFSclient build
      this.daos = new DaosFsClient.DaosFsClientBuilder().poolId(poolUuid).containerId(contUuid).ranks(svc).build();
      this.uri = URI.create(name.getScheme() + "://" + name.getAuthority());
      this.workingDir = new Path("/user", System.getProperty("user.name"))
              .makeQualified(this.uri, this.getWorkingDirectory());
      //mkdir workingDir in DAOS
      daos.mkdir(workingDir.toUri().getPath(), true);
      setConf(conf);
    } catch (IOException e) {
      throw new IOException("failed to initialize " + this.getClass().getName(), e);
    }
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

  @Override
  public FSDataInputStream open(
          Path f,
          final int bufferSize) throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosFileSystem open :  path = " + f.toUri().getPath() + " ; buffer size = " + bufferSize);
    }

    DaosFile file = daos.getFile(f.toUri().getPath());
    if (!file.exists()) {
      throw new FileNotFoundException(f + " not exist");
    }

    return new FSDataInputStream(new DaosInputStream(
            file, statistics, readBufferSize, preLoadBufferSize, bufferedReadEnabled));
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
    String key = f.toUri().getPath();

    DaosFile daosFile = this.daos.getFile(key);

    if (daosFile.exists() && (!daosFile.delete())) {
      throw new IOException("failed to delete existing file " + daosFile);
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

  @Override
  public boolean rename(Path src, Path dst) throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosFileSystem: rename old path {} to new path {}", src.toUri().getPath(), dst.toUri().getPath());
    }
    String srcPath = src.toUri().getPath();
    String destPath = dst.toUri().getPath();
    // determine  if src is root dir and whether it exits
    if (src.toUri().getPath().equals("/")) {
      if (LOG.isDebugEnabled()) {
        LOG.debug("DaosFileSystem:  can not rename root path {}", src);
      }
      throw new IOException("cannot move root / directory");
    }

    if (srcPath.equals(destPath)) {
      throw new IOException("dest and src paths are same. " + srcPath);
    }

    if (daos.exists(destPath)) {
      throw new IOException("dest file exists, " + destPath);
    }

    daos.move(srcPath, destPath);
    return true;
  }

  @Override
  public boolean delete(Path f, boolean recursive) throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosFileSystem:   delete  path = {} - recursive = {}", f.toUri().getPath(), recursive);
    }
    return daos.delete(f.toUri().getPath(), recursive);
  }

  @Override
  public FileStatus[] listStatus(Path f) throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosFileSystem listStatus :  List status for path = {}", f.toUri().getPath());
    }

    DaosFile file = daos.getFile(f.toUri().getPath());

    final List<FileStatus> result = Lists.newArrayList();
    try {
      if (file.isDirectory()) {
        String[] children = file.listChildren();
        if (children != null && children.length > 0) {
          for (String child : children) {
            FileStatus childStatus = getFileStatus(new Path(f, child).makeQualified(this.uri, this.workingDir),
                    daos.getFile(file, child));
            result.add(childStatus);
          }
        }
      } else {
        result.add(getFileStatus(f, file));
      }
    } catch (IOException e) {
      if (e instanceof DaosIOException) {
        DaosIOException de = (DaosIOException) e;
        if (de.getErrorCode() == com.intel.daos.client.Constants.ERROR_CODE_NOT_EXIST) {
          throw new FileNotFoundException(e.getMessage());
        }
      }
      throw e;
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
    String key = f.toUri().getPath();

    daos.mkdir(key, com.intel.daos.client.Constants.FILE_DEFAULT_FILE_MODE, true);
    return true;
  }

  @Override
  public FileStatus getFileStatus(Path f) throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosFileSystem getFileStatus:  Get File Status , path = {}", f.toUri().getPath());
    }
    String key = f.toUri().getPath();
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
    return new FileStatus(attributes.getLength(), !attributes.isFile(), 1,
            attributes.isFile() ? blockSize : 0, DaosUtils.toMilliSeconds(attributes.getModifyTime()), path);
  }

  @Override
  public boolean exists(Path f) {
    if (LOG.isDebugEnabled()) {
      LOG.debug(" DaosFileSystem exists: Is path = {} exists", f.toUri().getPath());
    }
    try {
      String key = f.toUri().getPath();
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
    if (daos != null) {
      daos.disconnect();
    }
    super.close();
  }

  public boolean isBufferedReadEnabled() {
    return bufferedReadEnabled;
  }
}
