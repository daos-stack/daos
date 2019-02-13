#Self-healing (Rebuild)

In DAOS, if the data are replicated with multiple copies,
once the target is failed, the data on it will be rebuilt
on the other targets automatically,so the data integrity
will not be impacted due to the target failure.

#Rebuild Detection

When a target failed, it should be detected promptly and
notify the pool (Raft) leader. Then the leader will exclude
the target from the pool and trigger the rebuild process
immediately.

##Current status

Currently, since the raft leader can not exclude the target
automatically, the sysadmin has to trigger the rebuild by
manually exclude the target from the pool.

##longterm goal

In future, the raft leader should be able to detect the
target failure promptly and then trigger the rebuild
automatically by itself, without the help of sysadmin.

##multiple pool rebuild

If there are multiple pools being impacted by the failing
target, these pools can be rebuilt concurrently.

##mulitple targets rebuild

If there are multiple targets in the same pool failed at
the same time, these targets can be rebuilt at the same time.
But if the pool is already being rebuilt, the failing target
has to wait to be rebuilt until the current pool rebuilt is
finished.

#Rebuild process

The rebuild is divided into 2 phases, scan and pull.

##Scan

Initially, the leader will propate the failure notification
to all other surviving targets by a collective RPC. Any target
that receives this RPC will start to scan its own object table
to determine the object losts data redundancy on the faulty
target, if it does, then send their IDs and related metadata
to the Rebuild Targets. As to how to choose the rebuild target
for faulty target,it will be described in placement/README.md

##pull

Once the rebuild targets get the object list from the scanning
target, it will pull the data of these objects from other
replicas and then write data locally. Each target will report
its rebuild status, rebuilding objects, records, is_finished?
etc, to the pool leader. Once the leader learned all of targets
finished its scanning and rebuilding, it will notify all targets
to release all of the resources holded during rebuild process.

##I/O during rebuild

For read, fetch, list etc, it will skip the rebuilding the target,
i.e. it will try to fetch the data from other replicas.

For write, update, punch etc, it will update the data both on the
rebuilding(new spare) target and other replicas, which might cause
the data being written twice on the rebuilding target, but it can
be handled internally by rebuild.

##rebuild resource throttle

During rebuild process, the user can set the throttle to guarantee
the rebuild will not use more resource than the user setting. The
user can only set the CPU cycle now. For example, if the user set
the throttle to 50, then the rebuild will at most use 50% of CPU
cycle to do rebuild job. The default rebuild throttle for CPU cycle
is 30.

#Rebuild status

As described earlier, each target will report its rebuild status to
the pool leader by IV, then the leader will summurize the status of
all targets, and print out the whole rebuild status by every 2 seconds,
for example these messages.

Rebuild [started] (pool 8799e471 ver=41)
Rebuild [scanning] (pool 8799e471 ver=41, toberb_obj=0, rb_obj=0, rec= 0, done 0 status 0 duration=0 secs)
Rebuild [queued] (419d9c11 ver=2)
Rebuild [started] (pool 419d9c11 ver=2)
Rebuild [scanning] (pool 419d9c11 ver=2, toberb_obj=0, rb_obj=0, rec= 0, done 0 status 0 duration=0 secs)
Rebuild [pulling] (pool 8799e471 ver=41, toberb_obj=75, rb_obj=75, rec= 11937, done 0 status 0 duration=10 secs)
Rebuild [completed] (pool 419d9c11 ver=2, toberb_obj=10, rb_obj=10, rec= 1026, done 1 status 0 duration=8 secs)
Rebuild [completed] (pool 8799e471 ver=41, toberb_obj=75, rb_obj=75, rec= 13184, done 1 status 0 duration=14 secs)

There are 2 pools being rebuilt (pool 8799e471 and pool 419d9c11,
note: only first 8 letters of the pool uuid are shown here).

The 1st line means the rebuild for pool 8799e471 is started, whose pool
map version is 41.
The 2nd line means the rebuild for pool 8799e471 is in scanning phase,
and no objects & records are being rebuilt yet.
The 3rd line means a rebuild job for pool 419d9c11 is being queued.
The 4th line means the rebuild for pool 419d9c11 is started, whose pool
map version is 2.
The 5th line means the rebuild for pool 419d9c11 is in scanning phase,
and no objects & records are being rebuilt yet.
The 6th line means the rebuild for pool 8799e471 is in pulling phase,
and there are 75 objects to be rebuilt(toberb_obj=75), and all of them
are rebuilt(rb_obj=75), but records rebuilt for these objects are not
finished yet(done 0) and only 11937 records (rec = 11937) are rebuilt.
The 7th line means the rebuild for pool 419d9c11 is done (done 1), and
there are totally 10 objects and 1026 records are rebuilt, which costs
about 8 seconds.
The 8th line means the rebuild for pool 8799e471 is done (done 1), and
there are totally 75 objects and 13184 records are rebuilt, which costs
about 14seconds.

During the rebuild, if the client query the pool status to the pool leader,
which will return its rebuild status to client as well.

struct daos_rebuild_status {
        /** pool map version in rebuilding or last completed rebuild */
        uint32_t                rs_version;
        /** padding bytes */
        uint32_t                rs_pad_32;
        /** errno for rebuild failure */
        int32_t                 rs_errno;
        /**
         * rebuild is done or not, it is valid only if @rs_version is non-zero
         */
        int32_t                 rs_done;
        /** # total to-be-rebuilt objects, it's non-zero and increase when
         * rebuilding in progress, when rs_done is 1 it will not change anymore
         * and should equal to rs_obj_nr. With both rs_toberb_obj_nr and
         * rs_obj_nr the user can know the progress of the rebuilding.
         */
        uint64_t                rs_toberb_obj_nr;
        /** # rebuilt objects, it's non-zero only if rs_done is 1 */
        uint64_t                rs_obj_nr;
        /** # rebuilt records, it's non-zero only if rs_done is 1 */
        uint64_t                rs_rec_nr;
};

#Rebuild failure

If the rebuild is failed due to some failures, it will be aborted, and the
related message will be shown on the leader console. for example

Rebuild [aborted] (pool 8799e471 ver=41, toberb_obj=75, rb_obj=75, rec= 11937, done 1 status 0 duration=10 secs)

Note: these rebuild can be resumed when the pool is reconnected.
