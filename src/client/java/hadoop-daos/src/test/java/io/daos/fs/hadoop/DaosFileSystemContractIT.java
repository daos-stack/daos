/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package io.daos.fs.hadoop;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.apache.hadoop.fs.*;
import org.junit.Test;

import java.io.IOException;

import static org.junit.Assume.assumeTrue;

public class DaosFileSystemContractIT extends FileSystemContractBaseTest {

  private static final Log LOG = LogFactory.getLog(DaosFileSystemContractIT.class);

  @Override
  protected void setUp() throws Exception {
    fs = DaosFSFactory.getFS();
    fs.mkdirs(new Path("/test"));
  }

  @Override
  protected void tearDown() throws Exception {
    super.tearDown();
  }

  @Override
  public void testMkdirsWithUmask() throws Exception {
    // not supported
  }

  @Test
  public void testRootDirAlwaysExists() throws Exception {
    //this will throw an exception if the path is not found
    fs.getFileStatus(super.path("/"));
    //this catches overrides of the base exists() method that don't
    //use getFileStatus() as an existence probe
    assertTrue("FileSystem.exists() fails for root",
        fs.exists(super.path("/")));
  }

  @Test
  public void testRenameRootDirForbidden() throws Exception {
    assumeTrue(renameSupported());
    try {
      rename(super.path("/"),
              super.path("/test/newRootDir"),
              false, true, false);
      fail("should throw IOException");
    }catch (IOException e){
    }
  }

  @Test
  public void testDeleteSubdir() throws Exception {
    Path parentDir = this.path("/test/hadoop");
    this.fs.mkdirs(parentDir);

    Path file = this.path("/test/hadoop/file");
    Path subdir = this.path("/test/hadoop/subdir");
    this.createFile(file);

    assertTrue("Created subdir", this.fs.mkdirs(subdir));
    assertTrue("File exists", this.fs.exists(file));
    assertTrue("Parent dir exists", this.fs.exists(parentDir));
    assertTrue("Subdir exists", this.fs.exists(subdir));

    assertTrue("Deleted subdir", this.fs.delete(subdir, true));
    assertTrue("Parent should exist", this.fs.exists(parentDir));

    assertTrue("Deleted file", this.fs.delete(file, false));
    assertTrue("Parent should exist", this.fs.exists(parentDir));
  }

  @Test
  protected boolean renameSupported() {
    return true;
  }

  @Override
  public void testRenameNonExistentPath() throws Exception {
    assumeTrue(renameSupported());
    Path src = this.path("/test/hadoop/path");
    Path dst = this.path("/test/new/newpath");
    try {
      super.rename(src, dst, false, false, false);
      fail("Should throw FileNotFoundException!");
    } catch (IOException e) {
      // expected
    }
  }

  @Override
  public void testRenameFileMoveToNonExistentDirectory() throws Exception {
    assumeTrue(renameSupported());
    Path src = this.path("/test/hadoop/file");
    this.fs.mkdirs(new Path("/test/hadoop"));
    this.createFile(src);
    Path dst = this.path("/test/new/newfile");
    try {
      super.rename(src, dst, false, true, false);
      fail("Should throw FileNotFoundException!");
    } catch (IOException e) {
      // expected
    }
  }

  @Test
  public void testRenameDirectoryConcurrent() throws Exception {
    assumeTrue(renameSupported());
    Path src = this.path("/test/hadoop/file/");
    this.fs.mkdirs(src);
    Path child1 = this.path("/test/hadoop/file/1");
    Path child2 = this.path("/test/hadoop/file/2");
    Path child3 = this.path("/test/hadoop/file/3");
    Path child4 = this.path("/test/hadoop/file/4");

    this.createFile(child1);
    this.createFile(child2);
    this.createFile(child3);
    this.createFile(child4);

    Path dst = this.path("/test/new");
    Path dstChild = new Path(dst, src.getName());
    super.rename(src, dst, true, false, true);
    assertEquals(4, this.fs.listStatus(dst).length);
    assertFalse(fs.exists(dstChild));

  }

  @Override
  public void testRenameDirectoryMoveToNonExistentDirectory() throws Exception {
    assumeTrue(renameSupported());
    Path src = this.path("/test/hadoop/dir");
    this.fs.mkdirs(src);
    Path dst = this.path("/test/new/newdir");
    try {
      super.rename(src, dst, false, true, false);
      fail("Should throw IOException!");
    } catch (IOException e) {
      // expected
    }
  }

  @Override
  public void testRenameFileMoveToExistingDirectory() throws Exception {
    if (!renameSupported()) return;

    Path src = path("/test/hadoop/file");
    this.fs.mkdirs(new Path("/test/hadoop"));
    createFile(src);
    Path dst = path("/test/new/newfile");
    this.fs.mkdirs(dst.getParent());
    rename(src, dst, true, false, true);
  }

  @Override
  public void testRenameFileAsExistingFile() throws Exception {
    assumeTrue(renameSupported());
    Path src = this.path("/test/hadoop/file");
    this.fs.mkdirs(new Path("/test/hadoop"));
    this.createFile(src);
    Path dst = this.path("/test/new/newfile");
    this.fs.mkdirs(new Path("/test/new"));
    this.createFile(dst);
    try {
      super.rename(src, dst, false, true, true);
      fail("Should throw IOException");
    } catch (IOException e) {
      // expected
    }
  }

  @Override
  public void testRenameDirectoryAsExistingFile() throws Exception {
    assumeTrue(renameSupported());
    Path src = this.path("/test/hadoop/dir");
    this.fs.mkdirs(src);
    Path dst = this.path("/test/new/newfile");
    this.fs.mkdirs(new Path("/test/new"));
    this.createFile(dst);
    try {
      super.rename(src, dst, false, true, true);
      fail("Should throw FileAlreadyExistsException");
    } catch (IOException e) {
      // expected
    }
  }

  @Test
  public void testGetFileStatusFileAndDirectory() throws Exception {
    Path filePath = this.path("/test/daos/file1");
    this.fs.mkdirs(new Path("/test/daos"));
    this.createFile(filePath);
    assertTrue("Should be file", this.fs.getFileStatus(filePath).isFile());
    assertFalse("Should not be directory",
        this.fs.getFileStatus(filePath).isDirectory());

    Path dirPath = this.path("/test/daos/dir");
    this.fs.mkdirs(dirPath);
    assertTrue("Should be directory",
        this.fs.getFileStatus(dirPath).isDirectory());
    assertFalse("Should not be file", this.fs.getFileStatus(dirPath).isFile());
  }

  @Test
  public void testMkdirsForExistingFile() throws Exception {
    Path testFile = this.path("/test/hadoop/file");
    assertFalse(this.fs.exists(testFile));
    this.fs.mkdirs(new Path("/test/hadoop"));
    this.createFile(testFile);
    assertTrue(this.fs.exists(testFile));
    this.fs.mkdirs(testFile);
  }

  @Override
  public void testRenameDirectoryMoveToExistingDirectory() throws Exception {
    if (!renameSupported()) return;

    Path src = path("/test/hadoop/dir");
    this.fs.mkdirs(src);
    createFile(path("/test/hadoop/dir/file1"));
    this.fs.mkdirs(new Path("/test/hadoop/dir/subdir"));
    createFile(path("/test/hadoop/dir/subdir/file2"));

    Path dst = path("/test/new/newdir");
    fs.mkdirs(dst.getParent());
    rename(src, dst, true, false, true);

    assertFalse("Nested file1 exists",
            fs.exists(path("/test/hadoop/dir/file1")));
    assertFalse("Nested file2 exists",
            fs.exists(path("/test/hadoop/dir/subdir/file2")));
    assertTrue("Renamed nested file1 exists",
            fs.exists(path("/test/new/newdir/file1")));
    assertTrue("Renamed nested exists",
            fs.exists(path("/test/new/newdir/subdir/file2")));
  }

  @Override
  public void testRenameDirectoryAsExistingDirectory() throws Exception {
    if (!renameSupported()) return;

    Path src = path("/test/hadoop/dir");
    fs.mkdirs(src);
    createFile(path("/test/hadoop/dir/file1"));
    this.fs.mkdirs(new Path("/test/hadoop/dir/subdir"));
    createFile(path("/test/hadoop/dir/subdir/file2"));

    Path parent = path("/test/new/");
    Path dst = path("/test/new/dir");
    fs.mkdirs(parent);
    rename(src, dst, true, false, true);
    assertTrue("Destination changed",
            fs.exists(path("/test/new/dir")));
    assertFalse("Nested file1 exists",
            fs.exists(path("/test/hadoop/dir/file1")));
    assertFalse("Nested file2 exists",
            fs.exists(path("/test/hadoop/dir/subdir/file2")));
    assertTrue("Renamed nested file1 exists",
            fs.exists(path("/test/new/dir")));
    assertTrue("Renamed nested exists",
            fs.exists(path("/test/new/dir/subdir/file2")));
  }

  @Override
  public void testInputStreamClosedTwice() throws IOException {
    //HADOOP-4760 according to Closeable#close() closing already-closed
    //streams should have no effect.
    Path src = path("/test/hadoop/file");
    this.fs.mkdirs(new Path("/test/hadoop"));
    createFile(src);
    FSDataInputStream in = fs.open(src);
    in.close();
    in.close();
  }

  @Override
  public void testRenameFileAsExistingDirectory() throws Exception {
    if (!renameSupported()) return;

    Path src = path("/test/hadoop/file");
    this.fs.mkdirs(new Path("/test/hadoop"));
    createFile(src);
    Path parent = new Path("/test/new/");
    Path dst = path("/test/new/file");
    this.fs.mkdirs(parent);
    rename(src, dst, true, false, true);
    assertTrue("Destination changed",
            fs.exists(path("/test/new/file")));
  }

  @Override
  public void testWriteInNonExistentDirectory() throws IOException {
    Path path = path("/test/hadoop/file");
    assertFalse("Parent exists", fs.exists(path.getParent()));
    this.fs.mkdirs(new Path("/test/hadoop"));
    createFile(path);

    assertTrue("Exists", fs.exists(path));
    assertEquals("Length", data.length, fs.getFileStatus(path).getLen());
    assertTrue("Parent exists", fs.exists(path.getParent()));
  }

 @Override
  public void testListStatus() throws Exception {
    Path[] testDirs = { path("/test/hadoop/a"),
            path("/test/hadoop/b"),
            path("/test/hadoop/c/1"), };
    assertFalse(fs.exists(testDirs[0]));

    for (Path path : testDirs) {
      assertTrue(fs.mkdirs(path));
    }

    FileStatus[] paths = fs.listStatus(path("/test"));
    assertEquals(1, paths.length);
    assertEquals(path("/test/hadoop"), paths[0].getPath());

    paths = fs.listStatus(path("/test/hadoop"));
    assertEquals(3, paths.length);
    // skip
//    assertEquals(path("/test/hadoop/a"), paths[0].getPath());
//    assertEquals(path("/test/hadoop/b"), paths[1].getPath());
//    assertEquals(path("/test/hadoop/c"), paths[2].getPath());

    paths = fs.listStatus(path("/test/hadoop/a"));
    assertEquals(0, paths.length);
  }

  @Override
  public void testOverwrite() throws IOException {
    // not supported
  }

  @Override
  public void testDeleteRecursively() throws IOException {
    Path dir = path("/test/hadoop");
    this.fs.mkdirs(dir);
    Path file = path("/test/hadoop/file");
    Path subdir = path("/test/hadoop/subdir");
    fs.mkdirs(subdir);

    createFile(file);
    assertTrue("Created subdir", fs.mkdirs(subdir));

    assertTrue("File exists", fs.exists(file));
    assertTrue("Dir exists", fs.exists(dir));
    assertTrue("Subdir exists", fs.exists(subdir));

    assertFalse("no delete", fs.delete(dir, false));
    assertTrue("File still exists", fs.exists(file));
    assertTrue("Dir still exists", fs.exists(dir));
    assertTrue("Subdir still exists", fs.exists(subdir));

    assertTrue("Deleted", fs.delete(dir, true));
    assertFalse("File doesn't exist", fs.exists(file));
    assertFalse("Dir doesn't exist", fs.exists(dir));
    assertFalse("Subdir doesn't exist", fs.exists(subdir));
  }

  @Override
  public void testOutputStreamClosedTwice() throws IOException {
    //HADOOP-4760 according to Closeable#close() closing already-closed
    //streams should have no effect.
    Path src = path("/test/hadoop/file");
    this.fs.mkdirs(new Path("/test/hadoop"));
    FSDataOutputStream out = fs.create(src);
    out.writeChar('H'); //write some data
    out.close();
    out.close();
  }

  /**
   * Write a dataset, read it back in and verify that they match.
   * Afterwards, the file is deleted.
   * @param len length of data
   * @throws IOException on IO failures
   */
  @Override
  protected void writeReadAndDelete(int len) throws IOException {
    Path path = path("/test/hadoop/file");
    this.fs.mkdirs(new Path("/test/hadoop"));
    writeAndRead(path, data, len, false, true);
  }


  @Override
  public void testOverWriteAndRead() throws Exception {
    // not supported
  }

}
