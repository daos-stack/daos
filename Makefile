daos_loc=/home/jvolivie/daos
daos_root=$(daos_loc)/install
prereq_root=$(daos_root)/prereq/release

vos: vos.o
	gcc -o vos vos.o -L$(prereq_root)/argobots/lib -Wl,-rpath=$(prereq_root)/argobots/lib -L$(daos_root)/lib64 -Wl,-rpath=$(daos_root)/lib64 -L$(daos_root)/lib64/daos_srv -Wl,-rpath=$(daos_root)/lib64/daos_srv -ldaos_common_pmem -lcart -lgurt -luuid -lvos -lbio -labt -g

vos.o: vos.c
	gcc vos.c -c  -I$(daos_root)/include -Isrc/include -I$(prereq_root)/argobots/include -I$(prereq_root)/protobufc/include -Wall -Werror -g

clean:
	rm -rf vos vos.o
