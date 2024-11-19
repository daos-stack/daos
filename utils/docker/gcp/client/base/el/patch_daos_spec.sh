#!/bin/bash

cat > daos_spec.patch <<EOF
diff --git a/utils/rpms/daos.spec b/utils/rpms/daos.spec
index ac6d591d7..8e0c4615b 100644
--- a/utils/rpms/daos.spec
+++ b/utils/rpms/daos.spec
@@ -321,7 +321,7 @@ This is the package that bridges the difference between the MOFED openmpi
 %build

 %define conf_dir %{_sysconfdir}/daos
-%if (0%{?rhel} == 8)
+%if (0%{?rhel} >= 8)
 %define scons_exe scons-3
 %else
 %define scons_exe scons
EOF

git apply daos_spec.patch
