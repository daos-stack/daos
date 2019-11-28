/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * <p>
 * http://www.apache.org/licenses/LICENSE-2.0
 * <p>
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.intel.daos.hadoop.fs;

import com.google.common.collect.Lists;
import com.intel.daos.*;
import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.*;
import org.apache.hadoop.fs.permission.FsPermission;
import org.apache.hadoop.util.Progressable;

import java.io.FileNotFoundException;
import java.io.IOException;
import java.net.URI;
import java.util.List;

/**
 * Implementation of {@link FileSystem} for DaosFileSystem,
 * used to access DAOS blob system in a filesystem style.
 */
public class DaosFileSystem extends FileSystem {
  private static final Log LOG =
          LogFactory.getLog(DaosFileSystem.class);

  private final String messageInfo = "TODO";
  private Path workingDir;
  private URI uri;
  private DaosFS client = null;
  private int readSize;
  private int writeSize;
  private int blockSize;

  public DaosFileSystem(){
  }

  @Override
  public void initialize(URI name, Configuration conf)
          throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosFileSystem initialize");
    }
    super.initialize(name, conf);
    try {
      this.readSize = conf.getInt(Constants.DAOS_READ_BUFFER_SIZE,
              Constants.DEFAULE_DAOS_READ_BUFFER_SIZE);
      this.writeSize = conf.getInt(Constants.DAOS_WRITE_BUFFER_SIZE,
              Constants.DEFAULT_DAOS_WRITE_BUFFER_SIZE);
      this.blockSize = conf.getInt(Constants.DAOS_BLOCK_SIZE,
              Constants.DEFAULT_DAOS_BLOCK_SIZE);

      String uuid = conf.get(Constants.DAOS_POOL_UUID);
      String mountpoint = conf.get(Constants.DAOS_CONTAINER_UUID);
      String svc = conf.get(Constants.DAOS_POOL_SVC);
      this.client = new DaosFS.Builder(false, false, false)
              .setPooluuid(uuid)
              .setContaineruuid(mountpoint)
              .setSvc(svc).build();
      this.uri = URI.create(name.getScheme() + "://" + name.getAuthority());
      this.workingDir = new Path("/user", System.getProperty("user.name")).
              makeQualified(this.uri, this.getWorkingDirectory());
      setConf(conf);
    } catch (DaosJavaException | DaosNativeException e) {
      LOG.error(e.getMessage());
      throw new IOException(e.getMessage());
    }
  }

  /**
   * Return the protocol scheme for the FileSystem.
   * <p/>
   *
   * @return <code>hdfs</code>
   */
  @Override
  public String getScheme() {
    return Constants.FS_DAOS;
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
      LOG.debug("oop path = " + f.toUri().getPath()
              +" ; buffer size = " + bufferSize);
    }
    String key = f.toUri().getPath();
    if (!exists(f)) {
      throw new FileNotFoundException(f + "no exists");
    }
    long length = 0;
    DaosFile daosFile;
    try {
      daosFile = client.getFile(key, true);
      length = daosFile.size();
      if (length < 0) {
        throw DaosUtils.translateException((int) length, "get file size failed , file path = "+key);
      }
    } catch (DaosJavaException | DaosNativeException e) {
      LOG.error(e.getMessage());
      throw new IOException(e.getMessage());
    }
    if(daosFile==null){
      throw new IOException("get daosFile failed");
    }
    return new FSDataInputStream(
            new DaosInputStream(daosFile, statistics, length, readSize));
  }

  @Override
  public FSDataOutputStream create(Path f,
                                   FsPermission permission,
                                   boolean overwrite,
                                   int bufferSize,
                                   short replication,
                                   long bs,
                                   Progressable progress)throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("create file , path= " + f.toUri().toString()
              + ", buffer size = " + bufferSize
              + ", block size = " + bs);
    }
    String key = f.toUri().getPath();
    if(!exists(f.getParent())){
      try {
        mkdirs(f.getParent(), new FsPermission("755"));
      }catch (IOException e){

      }
    }
    if (exists(f)) {
      throw new FileAlreadyExistsException(f + " already exists");
    }
    DaosFile  daosFile = null;
    try {
      daosFile = this.client.createFile(
              key,
              Constants.DAOS_MODLE,
              1024 * 1024,
              DaosObjClass.OC_SX);
    } catch (DaosNativeException e) {
      e.printStackTrace();
    } catch (DaosJavaException e) {
      e.printStackTrace();
    }
    if(daosFile==null){
      throw new IOException("get daosFile failed");
    }
    return new FSDataOutputStream(
            new DaosOutputStream(daosFile, key, statistics, writeSize));
  }

  @Override
  public FSDataOutputStream append(Path f,
                                   int bufferSize,
                                   Progressable progress)throws IOException {
    throw new IOException("Append is not supported");
  }

  @Override
  public boolean rename(Path src, Path dst) throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("rename file , old path=" +src.toUri().getPath()
              + "; new path = " +dst.toUri().getPath());
    }
    int rc = -1;
    if(src.toUri().getPath().equals("/")){
      return false;
    }
    FileStatus srcStatus = getFileStatus(src);
    FileStatus dstStatus;
    try {
      dstStatus = getFileStatus(dst);
    } catch (FileNotFoundException e) {
      dstStatus = null;
    }
    if (dstStatus == null) {
      // If dst doesn't exist, check whether dst dir exists or not
      try {
        dstStatus = getFileStatus(dst.getParent());
      } catch (FileNotFoundException e) {
        throw e;
      }
      if (!dstStatus.isDirectory()) {
        throw new IOException(String.format(
                "Failed to rename %s to %s, %s is a file",
                src, dst, dst.getParent()));
      }
    } else {
      if (srcStatus.getPath().equals(dstStatus.getPath())) {
        return !srcStatus.isDirectory();
      } else if (dstStatus.isDirectory()) {
        // If dst is a directory
        dst = new Path(dst, src.getName());
        FileStatus[] statuses;
        try {
          statuses = listStatus(dst);
        } catch (FileNotFoundException fnde) {
          statuses = null;
        }
        if (statuses != null && statuses.length > 0) {
          // If dst exists and not a directory / not empty
          throw new FileAlreadyExistsException(String.format(
                  "Failed to rename %s to %s, file already exists or not empty!", src, dst));
        }
      } else {
        // If dst is not a directory
        throw new FileAlreadyExistsException(String.format(
                "Failed to rename %s to %s, file already exists!", src, dst));
      }
    }
    rc = client.move(src.toUri().getPath(), dst.toUri().getPath());
    return rc == 0;
  }

  @Override
  public boolean delete(Path f, boolean recursive) throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("delete file , path= " + f.toUri().getPath());
    }

    FileStatus fileStatus;
    try {
      fileStatus = getFileStatus(f);
    } catch (FileNotFoundException e) {
      if (LOG.isDebugEnabled()) {
        LOG.debug("Couldn't delete " + f + " - does not exist");
      }
      return false;
    }
    int rc = client.remove(f.toUri().getPath());
    if(rc!=DaosBaseErr.SUCCESS.getValue()){
      throw DaosUtils.translateException(rc, "delete file failed , file path = "+f.toUri().getPath());
    }
    return rc == 0;
//    return true;
  }

  @Override
  public FileStatus[] listStatus(Path f) throws IOException{
    if (LOG.isDebugEnabled()) {
      LOG.debug("list Status , path = " + f.toUri().getPath());
    }
    final List<FileStatus> result = Lists.newArrayList();
    try {
      String key = f.toUri().getPath();
      final FileStatus fileStatus = getFileStatus(f);
      if(fileStatus==null) {
        return result.toArray(new FileStatus[result.size()]);
      }
      if (fileStatus.isDirectory()) {
        String[] fileString = client.getDir(key, false).list();
        for (String filePath : fileString) {
          if(key.equals("/")){
            FileStatus file =
                    getFileStatus(
                            new Path(key + filePath)
                                    .makeQualified(this.uri, this.workingDir));
            result.add(file);
          }else{
            FileStatus file =
                    getFileStatus(
                            new Path(key + "/" + filePath)
                                    .makeQualified(this.uri, this.workingDir));
            result.add(file);
          }
        }
      } else {
        result.add(fileStatus);
      }
    } catch (DaosJavaException | DaosNativeException e) {
      LOG.error(e.getMessage());
      throw new IOException(e.getMessage());
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
      LOG.debug("mkdirs dis , path= " + f.toUri().getPath());
    }
    String key = f.toUri().getPath();
    long rc = 0;
    try{
      FileStatus status = getFileStatus(f);
      // if the thread reaches here, there is something at the path
      if (status.isDirectory()) {
        return true;
      }else{
        throw new FileAlreadyExistsException("Not a directory: " + f);
      }
    }catch(FileNotFoundException e){
      rc =  client.createDir(key, Constants.DAOS_MODLE).getHandle();
      if(rc < 0){
        throw DaosUtils.translateException((int) rc, " mkdir failed , dir path = "+key);
      }
    }
    return rc > 0;
  }

  @Override
  public FileStatus getFileStatus(Path f) throws IOException {
    LOG.info("Get File Status , path= " + f.toUri().getPath());
    if (LOG.isDebugEnabled()) {
      LOG.debug("Get File Status , path= " + f.toUri().getPath());
    }
    String key = f.toUri().getPath();
    if(!key.isEmpty()){
      if (!client.ifExist(key)) {
        throw new FileNotFoundException("File does not exist: " + key);
      } else if (client.isDir(key)) {
        return new FileStatus(0, true, 1, this.blockSize, 0, f);
      } else {
        long length = client.getFile(key, true).size();
        if (length < 0) {
          throw DaosUtils.translateException((int) length, "get file size failed , file path = "+key);
        }
        return new FileStatus(length, false, 1, this.blockSize, 0, f);
      }
    }
    throw new FileNotFoundException("no exist path= " + f.toUri().getPath());
  }

  @Override
  public boolean exists(Path f) throws IOException {
    try {
      String key = f.toUri().getPath();
      return client.ifExist(key);
    } catch (DaosJavaException e) {
      return false;
    } catch (DaosNativeException e) {
      return false;
    }
  }

  @Override
  public void close() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("close");
    }
    super.close();
  }
}
