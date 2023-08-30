/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop;

import java.io.FileNotFoundException;
import java.io.IOException;
import java.net.URI;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import io.daos.*;
import io.daos.dfs.*;

import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.*;
import org.apache.hadoop.fs.permission.FsPermission;
import org.apache.hadoop.util.Progressable;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Implementation of {@link FileSystem} for DAOS file system.
 *
 * check resources/daos-config.txt for supported DAOS URIs and configurations.
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
  private String unsPrefix;
  private String qualifiedUriNoPrefix;
  private String qualifiedUriPath;
  private String qualifiedUnsWorkPath;
  private String workPath;

  private boolean withUnsPrefix;

  private boolean async = Constants.DEFAULT_DAOS_IO_ASYNC;

  static {
    if (ShutdownHookManager.removeHook(DaosClient.FINALIZER)) {
      org.apache.hadoop.util.ShutdownHookManager.get().addShutdownHook(DaosClient.FINALIZER, 0);
      if (LOG.isDebugEnabled()) {
        LOG.debug("daos finalizer relocated to hadoop ShutdownHookManager");
      }
    } else {
      LOG.warn("failed to relocate daos finalizer");
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
    DunsInfo info = searchUnsPath(name);
    if (info != null) {
      if (LOG.isDebugEnabled()) {
        LOG.debug("initializing from uns path, " + name);
      }
      initializeFromUns(name, conf, info);
      return;
    }
    throw new IllegalArgumentException("bad DAOS URI. " + name + "\n See supported DAOS URIs and configs: \n" +
        DaosFsConfig.getInstance().getConfigHelp());
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
    if (!"POSIX".equalsIgnoreCase(unsInfo.getLayout())) {
      throw new IllegalArgumentException("expect POSIX file system, but " + unsInfo.getLayout());
    }
    unsPrefix = unsInfo.getPrefix();
    withUnsPrefix = conf.getBoolean(Constants.DAOS_WITH_UNS_PREFIX, Constants.DEFAULT_DAOS_WITH_UNS_PREFIX);
    conf.set(Constants.DAOS_POOL_ID, unsInfo.getPoolId());
    conf.set(Constants.DAOS_CONTAINER_ID, unsInfo.getContId());
    super.initialize(name, conf);
    connectAndValidate(name, conf, unsInfo);
  }

  private void connectAndValidate(URI name, Configuration conf, DunsInfo unsInfo) throws IOException {
    // daosFSclient build
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder().poolId(unsInfo.getPoolId())
        .containerId(unsInfo.getContId());
    String svrGrp = conf.get(Constants.DAOS_SERVER_GROUP);
    if (!DaosUtils.isBlankStr(svrGrp)) {
      builder.serverGroup(svrGrp);
    }
    String poolFlags = conf.get(Constants.DAOS_POOL_FLAGS);
    if (!DaosUtils.isBlankStr(poolFlags)) {
      builder.poolFlags(Integer.valueOf(poolFlags));
    }
    try {
      this.daos = builder.build();
      qualifiedUriNoPrefix = name.getScheme() + "://" + (name.getAuthority() == null ? "" : name.getAuthority());
      qualifiedUriPath = qualifiedUriNoPrefix + "/" + unsPrefix;
      workPath = "/user/" + System.getProperty("user.name");
      this.uri = URI.create(qualifiedUriPath + "/");
      qualifiedUnsWorkPath = withUnsPrefix ? qualifiedUriPath + workPath : qualifiedUriNoPrefix + workPath;
      workingDir = new Path(qualifiedUnsWorkPath);
      // mkdir workingDir in DAOS
      daos.mkdir(workPath, true);
      getAndValidateDaosAttrs(name, conf);
      setConf(conf);
      if (LOG.isDebugEnabled()) {
        LOG.debug("DaosFileSystem initialized");
      }
    } catch (Exception e) {
      throw new IOException("failed to initialize " + this.getClass().getName(), e);
    }
  }

  /**
   * search UNS path from given <code>path</code> or its ancestors.
   *
   * @param uri
   * uri
   * @return DunsInfo
   * @throws IOException
   * {@link DaosIOException}
   */
  private DunsInfo searchUnsPath(URI uri) throws IOException {
    String path = uri.getPath();
    if ("/".equals(path) || !path.startsWith("/")) {
      return null;
    }
    // search UUID/Label or from file
    DunsInfo info = null;
    try {
      info = DaosUns.getAccessInfo(uri);
    } catch (DaosIOException e) {
      if (LOG.isDebugEnabled()) {
        LOG.debug("failed to search UNS path " + path, e);
      }
    }
    return info;
  }

  private void getAndValidateDaosAttrs(URI name, Configuration conf) {
    Map<String, String> allAttrs = daos.getUserDefAttributes();
    String daosChoice = allAttrs.getOrDefault(Constants.DAOS_CONFIG_CHOICE, "");
    String choice = conf.get(Constants.DAOS_CONFIG_CHOICE, daosChoice);
    DaosFsConfig.getInstance().merge(choice, conf, allAttrs);

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
    async = conf.getBoolean(Constants.DAOS_IO_ASYNC, Constants.DEFAULT_DAOS_IO_ASYNC);

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

    if (LOG.isDebugEnabled()) {
      LOG.debug("configs: ");
      LOG.debug("read buffer size " + readBufferSize);
      LOG.debug("write buffer size: " + writeBufferSize);
      LOG.debug("block size: " + blockSize);
      LOG.debug("chunk size: " + chunkSize);
      LOG.debug("min read size: " + minReadSize);
      LOG.debug("async: " + async);
    }
  }

  public String getUnsPrefix() {
    return unsPrefix;
  }

  @Override
  public int getDefaultPort() {
    return 0;
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
    // UNS path
    URI puri = p.toUri();
    if (puri.getScheme() != null || puri.getAuthority() != null) {
      return p;
    }
    String path = puri.getPath();
    if (withUnsPrefix) {
      if (!path.startsWith(unsPrefix)) {
        path = path.startsWith("/") ? (qualifiedUriPath + path) : (qualifiedUnsWorkPath + "/" + path);
      } else {
        path = qualifiedUriNoPrefix + path;
      }
    } else {
      path = removeUnsPrefix(puri);
    }
    return new Path(path);
  }

  private String removeUnsPrefix(URI puri) {
    String path = puri.getPath();
    if (!path.startsWith(unsPrefix)) {
      return path.startsWith("/") ? (qualifiedUriNoPrefix + path) :
              (qualifiedUnsWorkPath + (path.isEmpty() ? "" : ("/" + path)));
    }
    boolean truncated = false;
    if (path.length() > unsPrefix.length()) {
      path = path.substring(unsPrefix.length());
      truncated = true;
    } else {
      path = "/";
    }
    if (!path.startsWith("/")) {
      if (truncated) { // ensure correct uns prefix, counter example, <unsPrefix>abc, is not on uns path
        path = qualifiedUriNoPrefix + puri.getPath();
      } else {
        path = (qualifiedUnsWorkPath + "/" + path);
      }
    }
    return path;
  }

  private String getDaosRelativePath(Path path) {
    String oriPath = path.toUri().getPath();
    String p = oriPath;
    boolean truncated = false;
    if (p.startsWith(unsPrefix)) {
      if (p.length() > unsPrefix.length()) {
        p = p.substring(unsPrefix.length());
        truncated = true;
      } else {
        p = "/";
      }
    }
    if (!p.startsWith("/")) {
      if (truncated) { // ensure correct uns prefix, counter example, <unsPrefix>abc, is not on uns path
        return oriPath;
      }
      p = workPath + "/" + p;
    }
    return p;
  }

  @Override
  public Path makeQualified(Path path) {
    checkPath(path);
    return resolvePath(path);
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
            file, statistics, readBufferSize,
            bufferSize < minReadSize ? minReadSize : bufferSize, async));
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
            DaosObjectClass.OC_SX,
            this.chunkSize,
            true);

    return new FSDataOutputStream(new DaosOutputStream(daosFile, writeBufferSize, statistics, false, async),
        statistics);
  }

  @Override
  public FSDataOutputStream append(Path f,
                                   int bufferSize,
                                   Progressable progress) throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosFileSystem append file , path= " + f.toUri().toString() + ", buffer size = " + bufferSize);
    }
    String key = getDaosRelativePath(f);

    DaosFile daosFile = this.daos.getFile(key);
    return new FSDataOutputStream(new DaosOutputStream(daosFile, writeBufferSize, statistics, true, async),
        statistics);
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
    FileStatus[] statuses;
    // indicating root directory "/".
    if (f.toUri().getPath().equals("/")) {
      statuses = listStatus(f);
      boolean isEmptyDir = statuses.length <= 0;
      return rejectRootDirectoryDelete(isEmptyDir, recursive);
    }

    DaosFile file = daos.getFile(getDaosRelativePath(f));

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
        return file.delete(true);
      } else {
        statuses = listStatus(f);
        if (statuses != null && statuses.length > 0) {
          throw new IOException("DaosFileSystem delete : There are files in dir ");
        } else if (statuses != null && statuses.length == 0) {
          // delete empty dir
          return file.delete(false);
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
    final List<FileStatus> result = new ArrayList<>();
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
