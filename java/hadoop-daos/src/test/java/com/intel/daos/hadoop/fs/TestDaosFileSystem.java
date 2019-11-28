package com.intel.daos.hadoop.fs;

import org.apache.hadoop.fs.FileStatus;
import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.fs.permission.FsPermission;
import org.junit.AfterClass;
import org.junit.Assert;
import org.junit.BeforeClass;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.net.URI;

/**
 *
 */
public class TestDaosFileSystem {
  private static final Logger LOG =
          LoggerFactory.getLogger(TestDaosFileSystem.class);
  private static String testRootPath =
          TestDaosTestUtils.generateUniqueTestPath();
  private static CreateDaosFS daosFS;
  private static FileSystem fs;

  @BeforeClass
  public static void setup() throws IOException {
    System.out.println("@BeforeClass");
    daosFS=new CreateDaosFS();
    daosFS.getPool();
    daosFS.getContainer();
    fs = daosFS.getFs();
  }

  //every time test one
  @Test
  public void testInitialization() throws Exception{
    initializationTest("daos:///", "daos://null");
    initializationTest("daos://a:b@c", "daos://a:b@c");
    initializationTest("daos://a:b@c/", "daos://a:b@c");
    initializationTest("daos://a:b@c/path", "daos://a:b@c");
    initializationTest("daos://a@c", "daos://a@c");
    initializationTest("daos://a@c/", "daos://a@c");
    initializationTest("daos://a@c/path", "daos://a@c");
    initializationTest("daos://c", "daos://c");
    initializationTest("daos://c/", "daos://c");
    initializationTest("daos://c/path", "daos://c");
  }

  private void initializationTest(String initializationUri, String expectedUri) throws Exception{
    fs.initialize(URI.create(initializationUri), TestDaosTestUtils.getConfiguration());
    Assert.assertEquals(URI.create(expectedUri), fs.getUri());
  }


  @Test
  public void daosMkdirTest() throws IOException {
    Path f = new Path("/attempt_20191113114701_0024_m_000159_4855/part-00159-5404e5b0-a32d-41f9-bbb7-b9867e4e99ff-c000.csv");
    FsPermission permission = new FsPermission("0755");
    boolean status = fs.mkdirs(f, permission);
    System.out.println("mkdir result = "+status);
    permission = new FsPermission("0755");
    status = fs.mkdirs(f, permission);
    System.out.println("mkdir result = "+status);
  }

  @Test
  public void daosGetFileStatusTest() throws IOException {
    Path f = new Path("/");
    FileStatus fileStatus = fs.getFileStatus(f);
    if(fileStatus==null){
      System.out.println("Not file or directory");
    }
  }

  @Test
  public void daosRenameTest() throws IOException {
    Path oldpath = new Path("/test20");
    FsPermission permission = new FsPermission("0755");
    boolean status = fs.mkdirs(oldpath, permission);
    Path newPath = new Path("/test21");
    status =fs.rename(oldpath, newPath);
    System.out.println("rename path result = "+status);
  }

  @Test
  public void daosListTest() throws IOException {

    Path path = new Path("/");
    FileStatus[] status = fs.listStatus(path);
    for(FileStatus file : status){
      System.out.println("file = "+ file.toString());
    }
  }


  private Path setPath(String path) {
    if (path.startsWith("/")) {
      return new Path(testRootPath + path);
    } else {
      return new Path(testRootPath + "/" + path);
    }
  }

  @AfterClass
  public static void teardown() throws IOException {
    System.out.println("@AfterClass");
    fs.close();
    daosFS.close();
  }
}
