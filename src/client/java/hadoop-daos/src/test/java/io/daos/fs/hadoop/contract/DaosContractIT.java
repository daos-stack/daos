/**
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop.contract;

import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.contract.AbstractBondedFSContract;

public class DaosContractIT extends AbstractBondedFSContract {
  private static final String CONTRACT_XML = "contract/daos.xml";

  protected DaosContractIT(Configuration conf) {
    super(conf);
    addConfResource(CONTRACT_XML);
  }

  @Override
  public String getScheme() {
    return "daos";
  }
}
