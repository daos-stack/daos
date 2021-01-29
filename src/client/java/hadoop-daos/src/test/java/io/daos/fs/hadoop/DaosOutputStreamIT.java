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

import org.apache.hadoop.fs.FileAlreadyExistsException;
import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.fs.contract.ContractTestUtils;
import org.junit.*;
import org.junit.rules.Timeout;

import java.io.IOException;

import static org.junit.Assert.assertEquals;

/**
 *
 */
public class DaosOutputStreamIT {
  private static FileSystem fs;
  private static String testRootPath = DaosHadoopTestUtils.generateUniqueTestPath();

  @Rule
  public Timeout testTimeout = new Timeout(30 * 60 * 1000);

  @BeforeClass
  public static void setup() throws IOException {
    fs = DaosFSFactory.getFS();
    fs.mkdirs(new Path(testRootPath));
  }

  @AfterClass
  public static void tearDown() throws Exception {
    if (fs != null) {
      fs.delete(new Path(testRootPath), true);
    }
    fs.close();
  }

  private Path getTestPath() throws IOException {
    Path p = new Path(testRootPath + "/daos");
    if (!fs.exists(p)) {
      fs.mkdirs(p);
    }
    return p;
  }

  @Test
  public void testOutputStream2ExistingFile() throws IOException {
    long size = 1024;

    Path p = new Path("/test/data");
    fs.mkdirs(p);
    Path file = new Path(p, "1");
    ContractTestUtils.generateTestFile(fs, file, size, 256, 255);
    try {
      ContractTestUtils.generateTestFile(fs, file, size, 256, 255);
    } catch (FileAlreadyExistsException e) {
      // throw new FileAlreadyExistsException
    }
    Assert.assertEquals(1024, fs.listStatus(file)[0].getLen());
  }

  @Test
  public void testZeroByteUpload() throws IOException {
    ContractTestUtils.createAndVerifyFile(fs, getTestPath(), 0);
  }

  @Test
  public void testRegularUpload() throws IOException {
    FileSystem.clearStatistics();
    long size = 1024 * 1024;
    FileSystem.Statistics statistics =
        FileSystem.getStatistics("daos", DaosFileSystem.class);
    // This test is a little complicated for statistics, lifecycle is
    // generateTestFile
    //   fs.create(getFileStatus)    read 1
    //   output stream write         write 1
    // path exists(fs.exists)        read 1
    // verifyReceivedData
    //   fs.open(getFileStatus)      read 1
    //   input stream read           read 2(part size is 512K)
    // fs.delete
    //   getFileStatus & delete & exists & create fake dir read 2, write 2
    ContractTestUtils.createAndVerifyFile(fs, getTestPath(), size - 1);
    int readNum = (int) (size - 1) / Constants.DEFAULT_DAOS_READ_BUFFER_SIZE + 1;
    assertEquals(readNum, statistics.getReadOps());
    assertEquals(size - 1, statistics.getBytesRead());
    int writeNum = (int) (size - 1) / Constants.DEFAULT_DAOS_WRITE_BUFFER_SIZE + 1;
    assertEquals(writeNum, statistics.getWriteOps());
    assertEquals(size - 1, statistics.getBytesWritten());
  }
}
