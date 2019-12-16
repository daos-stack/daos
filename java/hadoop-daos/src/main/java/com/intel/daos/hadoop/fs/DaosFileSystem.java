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
import com.intel.daos.client.DaosFile;
import com.intel.daos.client.DaosFsClient;
import com.intel.daos.client.DaosObjectType;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.*;
import org.apache.hadoop.fs.permission.FsPermission;
import org.apache.hadoop.util.Progressable;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.FileNotFoundException;
import java.io.IOException;
import java.net.URI;
import java.util.List;

/**
 * Implementation of {@link FileSystem} for DaosFileSystem,
 * used to access DAOS blob system in a filesystem style.
 */
public class DaosFileSystem extends FileSystem {
  private static final Logger LOG = LoggerFactory.getLogger(DaosFileSystem.class);
  private Path workingDir;
  private URI uri;
  private DaosFsClient daos = null;
  private int readSize;
  private int writeSize;
  private int blockSize;
  private int chunksize;


  @Override
  public void initialize(URI name, Configuration conf)
          throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosFileSystem initialize");
    }
    super.initialize(name, conf);
    try {

      // get conf from hdfs-site.xml
      this.readSize = conf.getInt(Constants.DAOS_READ_BUFFER_SIZE, Constants.DEFAULE_DAOS_READ_BUFFER_SIZE);
      this.writeSize = conf.getInt(Constants.DAOS_WRITE_BUFFER_SIZE, Constants.DEFAULT_DAOS_WRITE_BUFFER_SIZE);
      this.blockSize = conf.getInt(Constants.DAOS_BLOCK_SIZE, Constants.DEFAULT_DAOS_BLOCK_SIZE);
      this.chunksize = conf.getInt(Constants.DAOS_CHUNK_SIZE, Constants.DEFAULT_DAOS_CHUNK_SIZE);
      String pooluuid = conf.get(Constants.DAOS_POOL_UUID);
      if(pooluuid==null) throw new IOException(Constants.DAOS_POOL_UUID + " is null , need to set up " + Constants.DAOS_POOL_UUID +" in hdfs.xml .");
      String contuuid = conf.get(Constants.DAOS_CONTAINER_UUID);
      if(contuuid==null) throw new IOException(Constants.DAOS_CONTAINER_UUID + " is null, need to set up " + Constants.DAOS_CONTAINER_UUID +" in hdfs.xml .");
      String svc = conf.get(Constants.DAOS_POOL_SVC);
      if(svc==null) throw new IOException(Constants.DAOS_POOL_SVC + " is null, need to set up " + Constants.DAOS_POOL_SVC +" in hdfs.xml .");

      // daosFSclient build
      this.daos = new DaosFsClient.DaosFsClientBuilder().poolId(pooluuid).containerId(contuuid).ranks(svc).build();
      this.uri = URI.create(name.getScheme() + "://" + name.getAuthority());
      this.workingDir = new Path("/user", System.getProperty("user.name")).
              makeQualified(this.uri, this.getWorkingDirectory());
      setConf(conf);
    } catch (IOException e) {
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
      LOG.debug("DaosFileSystem open :  path = " + f.toUri().getPath() +" ; buffer size = " + bufferSize);
    }
    String key = f.toUri().getPath();
    if (!exists(f)) {
      throw new FileNotFoundException(f + "no exists");
    }
    DaosFile daosFile = daos.getFile(key);
    long length = daosFile.length();
    if (daosFile==null||length < 0) {
         throw new IOException("get file size failed , file path = "+key);
    }
    return new FSDataInputStream(new DaosInputStream(daosFile, statistics, length, readSize));
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
      LOG.debug("DaosFileSystem create file , path= " + f.toUri().toString()
              + ", buffer size = " + bufferSize
              + ", block size = " + bs);
    }
    String key = f.toUri().getPath();

    DaosFile daosFile = this.daos.getFile(key);

    if (daosFile.exists()) {
      throw new FileAlreadyExistsException(f + " already exists");
    }

    daosFile.createNewFile(
              Constants.DAOS_MODLE,
              DaosObjectType.OC_SX,
              this.chunksize);

    if(daosFile==null){
      throw new IOException("get daosFile failed");
    }
    return new FSDataOutputStream(new DaosOutputStream(daosFile, key, statistics, writeSize));
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
      LOG.debug("DaosFileSystem: rename old path %s to new path %s" ,src.toUri().getPath(),dst.toUri().getPath());
    }
    // determine  if src is root dir and whether it exits
    if(src.toUri().getPath().equals("/")){
      if(LOG.isDebugEnabled()){
        LOG.debug("DaosFileSystem:  cat not rename root path %s",src);
      }
      return false;
    }

    FileStatus srcStatus = null;
    try{
      srcStatus = getFileStatus(src);
    }catch (FileNotFoundException e){
      throw new FileNotFoundException(String.format(
              "Failed to rename %s to %s, src dir do not !", src ,dst));
    }

    // determine dst
    FileStatus dstStatus = null;
    try {
      dstStatus = getFileStatus(dst);
      if(srcStatus.isDirectory()&&dstStatus.isFile()){
        // If dst exists and not a directory / not empty
        throw new FileAlreadyExistsException(String.format(
                "Failed to rename %s to %s, file already exists or not empty!",
                src, dst));
      }else if (srcStatus.getPath().equals(dstStatus.getPath())) {
        if(LOG.isDebugEnabled()){
          LOG.debug("DaosFileSystem:  src and dst refer to the same file or directory ");
        }
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
    } catch (FileNotFoundException e) {
      // If dst doesn't exist, check whether dst dir exists or not
      try {
        dstStatus = getFileStatus(dst.getParent());
        if (!dstStatus.isDirectory()) {
          throw new IOException(String.format(
                  "Failed to rename %s to %s, %s is a file",
                  src, dst, dst.getParent()));
        }
      } catch (FileNotFoundException e2) {
        throw new FileNotFoundException(String.format(
                "Failed to rename %s to %s, dst parent dir do not exists!", src ,dst));
      }
    }

    try {
      //Time to start
      if (LOG.isDebugEnabled()) {
        LOG.debug("DaosFileSystem:    renaming old file %s to new file %s" ,src.toUri().getPath(),dst.toUri().getPath());
      }
      if(srcStatus.isDirectory()){
        daos.getFile(src.toUri().getPath()).rename(dst.toUri().getPath());
      }else if(srcStatus.isFile()){
        daos.getFile(src.toUri().getPath()).rename(dst.toUri().getPath());
      }
    }catch (IOException e){
      return false;
    }
    return true;
  }

  @Override
  public boolean delete(Path f, boolean recursive) throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosFileSystem:   delete  path = %s - recursive = %s",f.toUri().getPath(),recursive);
    }

    FileStatus fileStatus;
    try {
      fileStatus = getFileStatus(f);
    } catch (FileNotFoundException e) {
      if (LOG.isDebugEnabled()) {
        LOG.debug("DaosFileSystem:  Couldn't delete " + f + " - does not exist");
      }
      return false;
    }

    if (fileStatus.isDirectory()) {
      if (LOG.isDebugEnabled()) {
        LOG.debug("DaosFileSystem: Path is a directory");
      }
      FileStatus[] statuses = null;
      try{
        if(recursive){
          // delete the dir and all files in the dir
          return daos.getFile(fileStatus.getPath().toUri().getPath()).delete(true);
        }else{
          statuses = listStatus(fileStatus.getPath());
          if (statuses != null && statuses.length > 0){
              throw new  IOException("DaosFileSystem delete : There are files in dir ");
          }else if(statuses != null && statuses.length == 0){
            // delete empty dir
            return  daos.getFile(f.toUri().getPath()).delete();
          }
        }
      }catch(FileNotFoundException e ){
        throw new FileNotFoundException(String.format(
                "Failed to delete %s , path do not exists!", f ));
      }
    }

    // delete file
    return daos.getFile(f.toUri().getPath()).delete();
  }

  @Override
  public FileStatus[] listStatus(Path f) throws IOException{
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosFileSystem listStatus :  List status for path = %s " ,f.toUri().getPath());
    }

    final List<FileStatus> result = Lists.newArrayList();
    try {
      String key = f.toUri().getPath();

      final FileStatus fileStatus = getFileStatus(f);
      if(fileStatus==null) {
        return result.toArray(new FileStatus[result.size()]);
      }
      if (fileStatus.isDirectory()) {
        if (LOG.isDebugEnabled()) {
          LOG.debug("DaosFileSystem listStatus : doing listFile for directory %s " , f.toUri().getPath());
        }
        String[] fileString = daos.getFile(key).listChildren();
        if(fileString==null||fileString.length <= 0 ){
          return result.toArray(new FileStatus[result.size()]);
        }
        for (String filePath : fileString) {
          if(filePath.equals("/")){
            FileStatus file =getFileStatus(new Path(key + filePath).makeQualified(this.uri, this.workingDir));
            result.add(file);
          }else{
            FileStatus file = getFileStatus(new Path(key + "/" + filePath).makeQualified(this.uri, this.workingDir));
            result.add(file);
          }
        }
      } else {
        result.add(fileStatus);
      }
    } catch (FileNotFoundException e) {
      throw new FileNotFoundException(e.getMessage());
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
      LOG.debug("DaosFileSystem mkdirs: Making directory = %s ",f.toUri().getPath());
    }
    String key = f.toUri().getPath();
    try{
      FileStatus status = getFileStatus(f);
      // if the thread reaches here, there is something at the path
      if (status.isDirectory()) {
        return true;
      }else{
        throw new FileAlreadyExistsException("Not a directory: " + f);
      }
    }catch(FileNotFoundException e){
      validatePath(f);
      daos.getFile(key).mkdirs();
      return true;
    }
  }

  /**
   * Check whether the path is a valid path.
   *
   * @param path the path to be checked.
   * @throws IOException
   */
  private void validatePath(Path path) throws IOException {
    Path fPart = path.getParent();
    do {
      try {
        FileStatus fileStatus = getFileStatus(fPart);
        if (fileStatus.isDirectory()) {
          // If path exists and a directory, exit
          break;
        } else {
          throw new FileAlreadyExistsException(String.format(
                  "Can't make directory for path '%s', it is a file.", fPart));
        }
      } catch (FileNotFoundException fnfe) {
      }
      fPart = fPart.getParent();
    } while (fPart != null);
  }

  @Override
  public FileStatus getFileStatus(Path f) throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosFileSystem getFileStatus:  Get File Status , path= %s ",f.toUri().getPath());
    }
    String key = f.toUri().getPath();
    DaosFile daosFile = daos.getFile(key);
    if(!key.isEmpty()&&daosFile!=null){
      if (!daosFile.exists()) {
        throw new FileNotFoundException("File does not exist: " + key);
      }
      if (daosFile.isDirectory()) {
        return new FileStatus(0, true, 1, this.blockSize, 0, f);
      } else {
        long length = daosFile.length();
        if (length < 0) {
          throw new IOException("get illegal file  , file path = " +key);
        }
        return new FileStatus(length, false, 1, this.blockSize, 0, f);
      }
    }
    throw new FileNotFoundException("no exist path= " + f.toUri().getPath());
  }

  @Override
  public boolean exists(Path f){
    if (LOG.isDebugEnabled()) {
      LOG.debug(" DaosFileSystem exists: Is path = %s exists",f.toUri().getPath());
    }
    try {
      String key = f.toUri().getPath();
      return daos.getFile(key).exists();
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
  }
}
