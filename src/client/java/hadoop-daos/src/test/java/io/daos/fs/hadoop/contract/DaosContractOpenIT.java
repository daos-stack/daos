/**
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop.contract;

import io.daos.fs.hadoop.DaosFSFactory;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.FSDataInputStream;
import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.fs.contract.AbstractContractOpenTest;
import org.apache.hadoop.fs.contract.AbstractFSContract;
import org.junit.Test;

public class DaosContractOpenIT extends AbstractContractOpenTest {

  @Override
  protected AbstractFSContract createContract(Configuration configuration) {
    configuration.addResource("daos-site.xml");
    DaosFSFactory.config(configuration);
    return new DaosContractIT(configuration);
  }

  @Test
  public void testOpenFileReadZeroByte() throws Throwable {
    describe("create & read a 0 byte file through the builders");
    Path path = path("zero.txt");
    FileSystem fs = getFileSystem();
    fs.createFile(path).overwrite(true).recursive().build().close();
    try (FSDataInputStream is = fs.openFile(path)
        .opt("fs.test.something", true)
        .opt("fs.test.something2", 3)
        .opt("fs.test.something3", "3")
        .build().get()) {
      assertMinusOne("initial byte read", is.read());
    }
  }
}
