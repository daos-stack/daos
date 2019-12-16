package com.intel.daos.hadoop.fs;

import org.apache.hadoop.fs.FileSystem;
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
  private static final Logger LOG = LoggerFactory.getLogger(TestDaosFileSystem.class);

  private static String testRootPath =
          TestDaosTestUtils.generateUniqueTestPath();
  private static FileSystem fs;

  @BeforeClass
  public static void setup() throws IOException {
    System.out.println("@BeforeClass");
    fs = DaosFSFactory.getFS();
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


  @AfterClass
  public static void teardown() throws IOException {
    System.out.println("@AfterClass");
    fs.close();
  }
}
