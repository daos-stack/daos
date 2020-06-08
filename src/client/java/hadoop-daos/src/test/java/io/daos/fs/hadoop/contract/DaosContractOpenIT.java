package io.daos.fs.hadoop.contract;

import io.daos.fs.hadoop.Constants;
import io.daos.fs.hadoop.DaosFSFactory;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.contract.AbstractContractOpenTest;
import org.apache.hadoop.fs.contract.AbstractFSContract;

public class DaosContractOpenIT extends AbstractContractOpenTest {

  @Override
  protected AbstractFSContract createContract(Configuration configuration) {
    configuration.addResource("daos-site.xml");
    configuration.set(Constants.DAOS_POOL_UUID, DaosFSFactory.pooluuid);
    configuration.set(Constants.DAOS_CONTAINER_UUID, DaosFSFactory.contuuid);
    configuration.set(Constants.DAOS_POOL_SVC, DaosFSFactory.svc);
    return new DaosContractIT(configuration);
  }
}
