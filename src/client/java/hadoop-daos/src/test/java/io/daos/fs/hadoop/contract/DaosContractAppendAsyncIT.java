/**
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop.contract;

import io.daos.fs.hadoop.DaosFSFactory;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.CommonPathCapabilities;
import org.apache.hadoop.fs.contract.AbstractContractAppendTest;
import org.apache.hadoop.fs.contract.AbstractFSContract;
import org.junit.Test;

import static org.apache.hadoop.fs.contract.ContractTestUtils.assertHasPathCapabilities;

public class DaosContractAppendAsyncIT extends AbstractContractAppendTest {
  @Override
  protected AbstractFSContract createContract(Configuration configuration) {
    configuration.addResource("daos-site.xml");
    DaosFSFactory.config(configuration, true);
    return new DaosContractIT(configuration);
  }

  @Test
  public void testFileSystemDeclaresCapability() throws Throwable {
    // not supported
  }
}
