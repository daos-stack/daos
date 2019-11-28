package com.intel.daos.hadoop.fs;//package org.apache.hadoop.fs;
//
//import org.apache.hadoop.conf.Configuration;
//import org.junit.AfterClass;
//import org.junit.BeforeClass;
//import org.junit.Test;
//
//import java.io.FileNotFoundException;
//import java.io.IOException;
//
//import static org.junit.Assume.assumeTrue;
//
//
//public class TestDaosFileSystemContract extends FileSystemContractBaseTest {
//	public static final String TEST_FS_DAOS_NAME = "test.fs.daos.name";
//
//  private  static CreateDaosFS daosFS ;
//  private static  Configuration conf;
//  private static FileSystem fsys;
//
//  private static FileSystem getFS(){
//    if(fsys==null){
//      conf = new Configuration();
//      daosFS = new CreateDaosFS();
//      conf.set("fs.daos.pool.uuid",daosFS.getPool());
//      conf.set("fs.daos.container.uuid",daosFS.getPool());
//      conf.set("test.fs.daos.name","daos://spark-tests/");
//      fsys=TestDaosTestUtils.createTestFileSystem(conf);
//
//    }
//    return fsys;
//  }
//
//  @Override
//  protected  void setUp() throws IOException {
//    System.out.println("@setup");
//    this.fs=getFS();
//    this.fs.mkdirs(new Path("/test"));
//
//
//  }
//
//  @Override
//  protected  void tearDown() throws  Exception {
//    System.out.println("@tearDown");
//    super.tearDown();
//  }
//
//  @Override
//  public void testMkdirsWithUmask() throws Exception {
//    // not supported
//    System.out.println("@Test");
//  }
//
//  @Test
//  public void testRootDirAlwaysExists() throws Exception {
//    System.out.println("@testRootDirAlwaysExists");
//    //this will throw an exception if the path is not found
//    if(fs==null){
//      System.out.println("fs null");
//    }
//    fs.getFileStatus(super.path("/"));
//    //this catches overrides of the base exists() method that don't
//    //use getFileStatus() as an existence probe
//    assertTrue("FileSystem.exists() fails for root",
//        fs.exists(super.path("/")));
//  }
//
//  @Test
//  public void testRenameRootDirForbidden() throws Exception {
//    assumeTrue(renameSupported());
//    rename(super.path("/"),
//        super.path("/test/newRootDir"),
//        false, true, false);
//  }
//
//  @Test
//  public void testDeleteSubdir() throws Exception {
//    Path parentDir = this.path("/test/hadoop");
//    Path file = this.path("/test/hadoop/file");
//    Path subdir = this.path("/test/hadoop/subdir");
//    this.createFile(file);
//
//    assertTrue("Created subdir", this.fs.mkdirs(subdir));
//    assertTrue("File exists", this.fs.exists(file));
//    assertTrue("Parent dir exists", this.fs.exists(parentDir));
//    assertTrue("Subdir exists", this.fs.exists(subdir));
//
//    assertTrue("Deleted subdir", this.fs.delete(subdir, true));
//    assertTrue("Parent should exist", this.fs.exists(parentDir));
//
//    assertTrue("Deleted file", this.fs.delete(file, false));
//    assertTrue("Parent should exist", this.fs.exists(parentDir));
//  }
//
//  @Test
//  protected boolean renameSupported() {
//    return true;
//  }
//
//  @Override
//  public void testRenameNonExistentPath() throws Exception {
//    assumeTrue(renameSupported());
//    Path src = this.path("/test/hadoop/path");
//    Path dst = this.path("/test/new/newpath");
//    try {
//      super.rename(src, dst, false, false, false);
//      fail("Should throw FileNotFoundException!");
//    } catch (FileNotFoundException e) {
//      // expected
//    }
//  }
//
//  @Override
//  public void testRenameFileMoveToNonExistentDirectory() throws Exception {
//    assumeTrue(renameSupported());
//    Path src = this.path("/test/hadoop/file");
//    this.createFile(src);
//    Path dst = this.path("/test/new/newfile");
//    try {
//      super.rename(src, dst, false, true, false);
//      fail("Should throw FileNotFoundException!");
//    } catch (FileNotFoundException e) {
//      // expected
//    }
//  }
//
//  @Test
//  public void testRenameDirectoryConcurrent() throws Exception {
//    assumeTrue(renameSupported());
//    Path src = this.path("/test/hadoop/file/");
//    Path child1 = this.path("/test/hadoop/file/1");
//    Path child2 = this.path("/test/hadoop/file/2");
//    Path child3 = this.path("/test/hadoop/file/3");
//    Path child4 = this.path("/test/hadoop/file/4");
//
//    this.createFile(child1);
//    this.createFile(child2);
//    this.createFile(child3);
//    this.createFile(child4);
//
//    Path dst = this.path("/test/new");
//    super.rename(src, dst, true, false, true);
//    assertEquals(4, this.fs.listStatus(dst).length);
//  }
//
//  @Override
//  public void testRenameDirectoryMoveToNonExistentDirectory() throws Exception {
//    assumeTrue(renameSupported());
//    Path src = this.path("/test/hadoop/dir");
//    this.fs.mkdirs(src);
//    Path dst = this.path("/test/new/newdir");
//    try {
//      super.rename(src, dst, false, true, false);
//      fail("Should throw FileNotFoundException!");
//    } catch (FileNotFoundException e) {
//      // expected
//    }
//  }
//
//  @Override
//  public void testRenameFileMoveToExistingDirectory() throws Exception {
//    super.testRenameFileMoveToExistingDirectory();
//  }
//
//  @Override
//  public void testRenameFileAsExistingFile() throws Exception {
//    assumeTrue(renameSupported());
//    Path src = this.path("/test12/hadoop/file");
//    this.createFile(src);
//    Path dst = this.path("/test12/new/newfile");
//    this.createFile(dst);
//    try {
//      super.rename(src, dst, false, true, true);
//      fail("Should throw FileAlreadyExistsException");
//    } catch (FileAlreadyExistsException e) {
//      // expected
//    }
//  }
//
//  @Override
//  public void testRenameDirectoryAsExistingFile() throws Exception {
//    assumeTrue(renameSupported());
//    Path src = this.path("/test/hadoop/dir");
//    this.fs.mkdirs(src);
//    Path dst = this.path("/test/new/newfile");
//    this.createFile(dst);
//    try {
//      super.rename(src, dst, false, true, true);
//      fail("Should throw FileAlreadyExistsException");
//    } catch (FileAlreadyExistsException e) {
//      // expected
//    }
//  }
//
//  @Test
//  public void testGetFileStatusFileAndDirectory() throws Exception {
//    Path filePath = this.path("/test/oss/file1");
//    this.createFile(filePath);
//    assertTrue("Should be file", this.fs.getFileStatus(filePath).isFile());
//    assertFalse("Should not be directory",
//        this.fs.getFileStatus(filePath).isDirectory());
//
//    Path dirPath = this.path("/test/oss/dir");
//    this.fs.mkdirs(dirPath);
//    assertTrue("Should be directory",
//        this.fs.getFileStatus(dirPath).isDirectory());
//    assertFalse("Should not be file", this.fs.getFileStatus(dirPath).isFile());
//  }
//
//  @Test
//  public void testMkdirsForExistingFile() throws Exception {
//    Path testFile = this.path("/test/hadoop/file");
//    assertFalse(this.fs.exists(testFile));
//    this.createFile(testFile);
//    assertTrue(this.fs.exists(testFile));
//    try {
//      this.fs.mkdirs(testFile);
//      fail("Should throw FileAlreadyExistsException!");
//    } catch (FileAlreadyExistsException e) {
//      // expected
//    }
//  }
//}
