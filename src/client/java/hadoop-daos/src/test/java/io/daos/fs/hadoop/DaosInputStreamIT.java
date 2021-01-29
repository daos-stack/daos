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

package io.daos.fs.hadoop;

import org.apache.hadoop.fs.FSDataInputStream;
import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.fs.contract.ContractTestUtils;
import org.apache.hadoop.io.IOUtils;
import org.junit.*;
import org.junit.rules.Timeout;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;

import static org.junit.Assert.assertTrue;

/**
 *
 */
public class DaosInputStreamIT {
  private static final Logger LOG = LoggerFactory.getLogger(DaosInputStreamIT.class);
  private static FileSystem fs;
  private static String testRootPath = DaosHadoopTestUtils.generateUniqueTestPath();

  @Rule
  public Timeout testTimeout = new Timeout(30 * 60 * 1000);

  @Before
  public void setup() throws IOException {
    fs = DaosFSFactory.getFS();
    fs.mkdirs(new Path(testRootPath));
  }

  @After
  public void tearDown() throws Exception {
    if (fs != null) {
      fs.delete(new Path(testRootPath), true);
    }
  }

  @Test
  public void testHeavyWrite() throws IOException {
    int numFiles = 100;
    long size = 1024;

    Path p = new Path("/test34546/data");
    fs.mkdirs(p);
    List<Path> files = new ArrayList<>();

    for (int i = 0; i < numFiles; i++) {
      Path file = new Path(p, "" + i);
      files.add(file);
    }
    for (Path file : files) {
      ContractTestUtils.generateTestFile(fs, file, size, 256, 128);
      Assert.assertTrue(fs.exists(file));
    }
  }

  private Path setPath(String path) throws IOException {
    Path p;
    if (path.startsWith("/")) {
      p = new Path(testRootPath + path);
    } else {
      p = new Path(testRootPath + "/" + path);
    }
    if (!fs.exists(p.getParent())) {
      fs.mkdirs(p.getParent());
    }
    return p;
  }

  @Test
  public void testSeekFile() throws Exception {
    Path smallSeekFile = setPath("/test.txt");
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
    long size = 2 * 1024 * 1024;

    ContractTestUtils.generateTestFile(fs, readTestFile, size, 256, 255);
    LOG.info(sizeFlag + "MB file created: /test/" + filename);

    FSDataInputStream instream = fs.open(readTestFile);
    byte[] buf = new byte[bufLen];
    long bytesRead = 0;
    while (bytesRead < size) {
      int bytes = 0;
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

  @Test
  public void testSequentialAndRandomRead() throws Exception {
    Path smallSeekFile = setPath("/test/smallSeekFile.txt");
    long size = 5 * 1024 * 1024;

    ContractTestUtils.generateTestFile(this.fs, smallSeekFile, size, 256, 255);
    LOG.info("5MB file created: smallSeekFile.txt");

    FSDataInputStream fsDataInputStream = this.fs.open(smallSeekFile);
    assertTrue("expected position at:" + 0 + ", but got:"
        + fsDataInputStream.getPos(), fsDataInputStream.getPos() == 0);
    DaosInputStream in =
        (DaosInputStream) fsDataInputStream.getWrappedStream();
    byte[] buf = new byte[Constants.MINIMUM_DAOS_READ_BUFFER_SIZE];
    in.read(buf, 0, Constants.MINIMUM_DAOS_READ_BUFFER_SIZE);
    assertTrue("expected position at:"
            + Constants.DEFAULT_DAOS_READ_BUFFER_SIZE + ", but got:"
            + in.getPos(),
        in.getPos() == Constants.MINIMUM_DAOS_READ_BUFFER_SIZE);

    fsDataInputStream.seek(4 * 1024 * 1024);
    buf = new byte[1 * 1024 * 1024];
    in.read(buf, 0, 1 * 1024 * 1024);
    assertTrue("expected position at:" + size + ", but got:"
            + in.getPos(),
        in.getPos() == size);

    IOUtils.closeStream(fsDataInputStream);
  }
}
