# DAOS Allocator for VOS

The DAV allocator for md_on_ssd phase 2 now supports evictable zones. This introduces change in the
layout of heap and is not compatible with the DAV allocator of phase 1. In order to support both
layouts the new allocator is packaged as a different library and linked to daos_common_pmem
library.
