package com.intel.daos.hadoop.fs;

import org.apache.hadoop.fs.FSDataInputStream;
import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.fs.contract.ContractTestUtils;
import org.apache.hadoop.io.IOUtils;
import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.Timeout;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.util.Random;

import static org.junit.Assert.assertTrue;

/**
 *
 */
public class TestDaosInputStream {
  private static final Logger LOG =
      LoggerFactory.getLogger(TestDaosInputStream.class);
  private static FileSystem fs;
  private static String testRootPath =
      TestDaosTestUtils.generateUniqueTestPath();
  private static CreateDaosFS daosFS;

  @Rule
  public Timeout testTimeout = new Timeout(30 * 60 * 1000);

  @BeforeClass
  public static void setup() throws IOException {
    System.out.println("@BeforeClass");
    daosFS=new CreateDaosFS();
    daosFS.getPool();
    daosFS.getContainer();
    fs = daosFS.getFs();
  }

  @AfterClass
  public static void tearDown() throws Exception {
    System.out.println("@AfterClass");
    if (fs != null) {
      fs.delete(new Path(testRootPath), true);
    }
    fs.close();
    daosFS.close();
  }

  private Path setPath(String path) {
    if (path.startsWith("/")) {
      return new Path(testRootPath + path);
    } else {
      return new Path(testRootPath + "/" + path);
    }
  }

  @Test
  public void testSeekFile() throws Exception {
    Path smallSeekFile = setPath("/test18.txt");
    long size = 5 * 1024 * 1024;

    ContractTestUtils.generateTestFile(fs, smallSeekFile, size, 256, 255);
    LOG.info("5MB file created: smallSeekFile.txt");

    FSDataInputStream instream = fs.open(smallSeekFile);
    int seekTimes = 5;
    LOG.info("multiple fold position seeking test...:");
    for (int i = 0; i < seekTimes; i++) {
      long pos = size / (seekTimes - i) - 1;
      LOG.info("begin seeking for pos: " + pos);
      instream.seek(pos);
      assertTrue("expected position at:" + pos + ", but got:"
          + instream.getPos(), instream.getPos() == pos);
      LOG.info("completed seeking at pos: " + instream.getPos());
    }
    LOG.info("random position seeking test...:");
    Random rand = new Random();
    for (int i = 0; i < seekTimes; i++) {
      long pos = Math.abs(rand.nextLong()) % size;
      LOG.info("begin seeking for pos: " + pos);
      instream.seek(pos);
      assertTrue("expected position at:" + pos + ", but got:"
          + instream.getPos(), instream.getPos() == pos);
      LOG.info("completed seeking at pos: " + instream.getPos());
    }
    IOUtils.closeStream(instream);
  }

  @Test
  public void testReadFile() throws Exception {
    final int bufLen = 256;
    final int sizeFlag = 5;
    String filename = "readTestFile_" + sizeFlag + ".txt";
    Path readTestFile = setPath("/test/" + filename);
    long size = sizeFlag * 1024 * 1024;

    ContractTestUtils.generateTestFile(fs, readTestFile, size, 256, 255);
    LOG.info(sizeFlag + "MB file created: /test/" + filename);

    FSDataInputStream instream = fs.open(readTestFile);
    byte[] buf = new byte[bufLen];
    long bytesRead = 0;
    while (bytesRead < size) {
      int bytes;
      if (size - bytesRead < bufLen) {
        int remaining = (int) (size - bytesRead);
        bytes = instream.read(buf, 0, remaining);
      } else {
        bytes = instream.read(buf, 0, bufLen);
      }
      bytesRead += bytes;

      if (bytesRead % (1024 * 1024) == 0) {
        int available = instream.available();
        int remaining = (int) (size - bytesRead);
        assertTrue("expected remaining:" + remaining + ", but got:" + available,
            remaining == available);
        LOG.info("Bytes read: " + Math.round((double) bytesRead / (1024 * 1024))
            + " MB");
      }
    }
    assertTrue(instream.available() == 0);
    IOUtils.closeStream(instream);
  }
}
