// go run mksysctl_openbsd.go
// Code generated by the command above; DO NOT EDIT.

//go:build arm && openbsd
// +build arm,openbsd

package unix

type mibentry struct {
	ctlname string
	ctloid  []_C_int
}

var sysctlMib = []mibentry{
	{"ddb.console", []_C_int{9, 6}},
	{"ddb.log", []_C_int{9, 7}},
	{"ddb.max_line", []_C_int{9, 3}},
	{"ddb.max_width", []_C_int{9, 2}},
	{"ddb.panic", []_C_int{9, 5}},
	{"ddb.profile", []_C_int{9, 9}},
	{"ddb.radix", []_C_int{9, 1}},
	{"ddb.tab_stop_width", []_C_int{9, 4}},
	{"ddb.trigger", []_C_int{9, 8}},
	{"fs.posix.setuid", []_C_int{3, 1, 1}},
	{"hw.allowpowerdown", []_C_int{6, 22}},
	{"hw.byteorder", []_C_int{6, 4}},
	{"hw.cpuspeed", []_C_int{6, 12}},
	{"hw.diskcount", []_C_int{6, 10}},
	{"hw.disknames", []_C_int{6, 8}},
	{"hw.diskstats", []_C_int{6, 9}},
	{"hw.machine", []_C_int{6, 1}},
	{"hw.model", []_C_int{6, 2}},
	{"hw.ncpu", []_C_int{6, 3}},
	{"hw.ncpufound", []_C_int{6, 21}},
	{"hw.ncpuonline", []_C_int{6, 25}},
	{"hw.pagesize", []_C_int{6, 7}},
	{"hw.perfpolicy", []_C_int{6, 23}},
	{"hw.physmem", []_C_int{6, 19}},
	{"hw.power", []_C_int{6, 26}},
	{"hw.product", []_C_int{6, 15}},
	{"hw.serialno", []_C_int{6, 17}},
	{"hw.setperf", []_C_int{6, 13}},
	{"hw.smt", []_C_int{6, 24}},
	{"hw.usermem", []_C_int{6, 20}},
	{"hw.uuid", []_C_int{6, 18}},
	{"hw.vendor", []_C_int{6, 14}},
	{"hw.version", []_C_int{6, 16}},
	{"kern.allowdt", []_C_int{1, 65}},
	{"kern.allowkmem", []_C_int{1, 52}},
	{"kern.argmax", []_C_int{1, 8}},
	{"kern.audio", []_C_int{1, 84}},
	{"kern.boottime", []_C_int{1, 21}},
	{"kern.bufcachepercent", []_C_int{1, 72}},
	{"kern.ccpu", []_C_int{1, 45}},
	{"kern.clockrate", []_C_int{1, 12}},
	{"kern.consbuf", []_C_int{1, 83}},
	{"kern.consbufsize", []_C_int{1, 82}},
	{"kern.consdev", []_C_int{1, 75}},
	{"kern.cp_time", []_C_int{1, 40}},
	{"kern.cp_time2", []_C_int{1, 71}},
	{"kern.cpustats", []_C_int{1, 85}},
	{"kern.domainname", []_C_int{1, 22}},
	{"kern.file", []_C_int{1, 73}},
	{"kern.forkstat", []_C_int{1, 42}},
	{"kern.fscale", []_C_int{1, 46}},
	{"kern.fsync", []_C_int{1, 33}},
	{"kern.global_ptrace", []_C_int{1, 81}},
	{"kern.hostid", []_C_int{1, 11}},
	{"kern.hostname", []_C_int{1, 10}},
	{"kern.intrcnt.nintrcnt", []_C_int{1, 63, 1}},
	{"kern.job_control", []_C_int{1, 19}},
	{"kern.malloc.buckets", []_C_int{1, 39, 1}},
	{"kern.malloc.kmemnames", []_C_int{1, 39, 3}},
	{"kern.maxclusters", []_C_int{1, 67}},
	{"kern.maxfiles", []_C_int{1, 7}},
	{"kern.maxlocksperuid", []_C_int{1, 70}},
	{"kern.maxpartitions", []_C_int{1, 23}},
	{"kern.maxproc", []_C_int{1, 6}},
	{"kern.maxthread", []_C_int{1, 25}},
	{"kern.maxvnodes", []_C_int{1, 5}},
	{"kern.mbstat", []_C_int{1, 59}},
	{"kern.msgbuf", []_C_int{1, 48}},
	{"kern.msgbufsize", []_C_int{1, 38}},
	{"kern.nchstats", []_C_int{1, 41}},
	{"kern.netlivelocks", []_C_int{1, 76}},
	{"kern.nfiles", []_C_int{1, 56}},
	{"kern.ngroups", []_C_int{1, 18}},
	{"kern.nosuidcoredump", []_C_int{1, 32}},
	{"kern.nprocs", []_C_int{1, 47}},
	{"kern.nthreads", []_C_int{1, 26}},
	{"kern.numvnodes", []_C_int{1, 58}},
	{"kern.osrelease", []_C_int{1, 2}},
	{"kern.osrevision", []_C_int{1, 3}},
	{"kern.ostype", []_C_int{1, 1}},
	{"kern.osversion", []_C_int{1, 27}},
	{"kern.pfstatus", []_C_int{1, 86}},
	{"kern.pool_debug", []_C_int{1, 77}},
	{"kern.posix1version", []_C_int{1, 17}},
	{"kern.proc", []_C_int{1, 66}},
	{"kern.rawpartition", []_C_int{1, 24}},
	{"kern.saved_ids", []_C_int{1, 20}},
	{"kern.securelevel", []_C_int{1, 9}},
	{"kern.seminfo", []_C_int{1, 61}},
	{"kern.shminfo", []_C_int{1, 62}},
	{"kern.somaxconn", []_C_int{1, 28}},
	{"kern.sominconn", []_C_int{1, 29}},
	{"kern.splassert", []_C_int{1, 54}},
	{"kern.stackgap_random", []_C_int{1, 50}},
	{"kern.sysvipc_info", []_C_int{1, 51}},
	{"kern.sysvmsg", []_C_int{1, 34}},
	{"kern.sysvsem", []_C_int{1, 35}},
	{"kern.sysvshm", []_C_int{1, 36}},
	{"kern.timecounter.choice", []_C_int{1, 69, 4}},
	{"kern.timecounter.hardware", []_C_int{1, 69, 3}},
	{"kern.timecounter.tick", []_C_int{1, 69, 1}},
	{"kern.timecounter.timestepwarnings", []_C_int{1, 69, 2}},
	{"kern.timeout_stats", []_C_int{1, 87}},
	{"kern.tty.tk_cancc", []_C_int{1, 44, 4}},
	{"kern.tty.tk_nin", []_C_int{1, 44, 1}},
	{"kern.tty.tk_nout", []_C_int{1, 44, 2}},
	{"kern.tty.tk_rawcc", []_C_int{1, 44, 3}},
	{"kern.tty.ttyinfo", []_C_int{1, 44, 5}},
	{"kern.ttycount", []_C_int{1, 57}},
	{"kern.utc_offset", []_C_int{1, 88}},
	{"kern.version", []_C_int{1, 4}},
	{"kern.video", []_C_int{1, 89}},
	{"kern.watchdog.auto", []_C_int{1, 64, 2}},
	{"kern.watchdog.period", []_C_int{1, 64, 1}},
	{"kern.witnesswatch", []_C_int{1, 53}},
	{"kern.wxabort", []_C_int{1, 74}},
	{"net.bpf.bufsize", []_C_int{4, 31, 1}},
	{"net.bpf.maxbufsize", []_C_int{4, 31, 2}},
	{"net.inet.ah.enable", []_C_int{4, 2, 51, 1}},
	{"net.inet.ah.stats", []_C_int{4, 2, 51, 2}},
	{"net.inet.carp.allow", []_C_int{4, 2, 112, 1}},
	{"net.inet.carp.log", []_C_int{4, 2, 112, 3}},
	{"net.inet.carp.preempt", []_C_int{4, 2, 112, 2}},
	{"net.inet.carp.stats", []_C_int{4, 2, 112, 4}},
	{"net.inet.divert.recvspace", []_C_int{4, 2, 258, 1}},
	{"net.inet.divert.sendspace", []_C_int{4, 2, 258, 2}},
	{"net.inet.divert.stats", []_C_int{4, 2, 258, 3}},
	{"net.inet.esp.enable", []_C_int{4, 2, 50, 1}},
	{"net.inet.esp.stats", []_C_int{4, 2, 50, 4}},
	{"net.inet.esp.udpencap", []_C_int{4, 2, 50, 2}},
	{"net.inet.esp.udpencap_port", []_C_int{4, 2, 50, 3}},
	{"net.inet.etherip.allow", []_C_int{4, 2, 97, 1}},
	{"net.inet.etherip.stats", []_C_int{4, 2, 97, 2}},
	{"net.inet.gre.allow", []_C_int{4, 2, 47, 1}},
	{"net.inet.gre.wccp", []_C_int{4, 2, 47, 2}},
	{"net.inet.icmp.bmcastecho", []_C_int{4, 2, 1, 2}},
	{"net.inet.icmp.errppslimit", []_C_int{4, 2, 1, 3}},
	{"net.inet.icmp.maskrepl", []_C_int{4, 2, 1, 1}},
	{"net.inet.icmp.rediraccept", []_C_int{4, 2, 1, 4}},
	{"net.inet.icmp.redirtimeout", []_C_int{4, 2, 1, 5}},
	{"net.inet.icmp.stats", []_C_int{4, 2, 1, 7}},
	{"net.inet.icmp.tstamprepl", []_C_int{4, 2, 1, 6}},
	{"net.inet.igmp.stats", []_C_int{4, 2, 2, 1}},
	{"net.inet.ip.arpdown", []_C_int{4, 2, 0, 40}},
	{"net.inet.ip.arpqueued", []_C_int{4, 2, 0, 36}},
	{"net.inet.ip.arptimeout", []_C_int{4, 2, 0, 39}},
	{"net.inet.ip.encdebug", []_C_int{4, 2, 0, 12}},
	{"net.inet.ip.forwarding", []_C_int{4, 2, 0, 1}},
	{"net.inet.ip.ifq.congestion", []_C_int{4, 2, 0, 30, 4}},
	{"net.inet.ip.ifq.drops", []_C_int{4, 2, 0, 30, 3}},
	{"net.inet.ip.ifq.len", []_C_int{4, 2, 0, 30, 1}},
	{"net.inet.ip.ifq.maxlen", []_C_int{4, 2, 0, 30, 2}},
	{"net.inet.ip.maxqueue", []_C_int{4, 2, 0, 11}},
	{"net.inet.ip.mforwarding", []_C_int{4, 2, 0, 31}},
	{"net.inet.ip.mrtmfc", []_C_int{4, 2, 0, 37}},
	{"net.inet.ip.mrtproto", []_C_int{4, 2, 0, 34}},
	{"net.inet.ip.mrtstats", []_C_int{4, 2, 0, 35}},
	{"net.inet.ip.mrtvif", []_C_int{4, 2, 0, 38}},
	{"net.inet.ip.mtu", []_C_int{4, 2, 0, 4}},
	{"net.inet.ip.mtudisc", []_C_int{4, 2, 0, 27}},
	{"net.inet.ip.mtudisctimeout", []_C_int{4, 2, 0, 28}},
	{"net.inet.ip.multipath", []_C_int{4, 2, 0, 32}},
	{"net.inet.ip.portfirst", []_C_int{4, 2, 0, 7}},
	{"net.inet.ip.porthifirst", []_C_int{4, 2, 0, 9}},
	{"net.inet.ip.porthilast", []_C_int{4, 2, 0, 10}},
	{"net.inet.ip.portlast", []_C_int{4, 2, 0, 8}},
	{"net.inet.ip.redirect", []_C_int{4, 2, 0, 2}},
	{"net.inet.ip.sourceroute", []_C_int{4, 2, 0, 5}},
	{"net.inet.ip.stats", []_C_int{4, 2, 0, 33}},
	{"net.inet.ip.ttl", []_C_int{4, 2, 0, 3}},
	{"net.inet.ipcomp.enable", []_C_int{4, 2, 108, 1}},
	{"net.inet.ipcomp.stats", []_C_int{4, 2, 108, 2}},
	{"net.inet.ipip.allow", []_C_int{4, 2, 4, 1}},
	{"net.inet.ipip.stats", []_C_int{4, 2, 4, 2}},
	{"net.inet.pfsync.stats", []_C_int{4, 2, 240, 1}},
	{"net.inet.tcp.ackonpush", []_C_int{4, 2, 6, 13}},
	{"net.inet.tcp.always_keepalive", []_C_int{4, 2, 6, 22}},
	{"net.inet.tcp.baddynamic", []_C_int{4, 2, 6, 6}},
	{"net.inet.tcp.drop", []_C_int{4, 2, 6, 19}},
	{"net.inet.tcp.ecn", []_C_int{4, 2, 6, 14}},
	{"net.inet.tcp.ident", []_C_int{4, 2, 6, 9}},
	{"net.inet.tcp.keepidle", []_C_int{4, 2, 6, 3}},
	{"net.inet.tcp.keepinittime", []_C_int{4, 2, 6, 2}},
	{"net.inet.tcp.keepintvl", []_C_int{4, 2, 6, 4}},
	{"net.inet.tcp.mssdflt", []_C_int{4, 2, 6, 11}},
	{"net.inet.tcp.reasslimit", []_C_int{4, 2, 6, 18}},
	{"net.inet.tcp.rfc1323", []_C_int{4, 2, 6, 1}},
	{"net.inet.tcp.rfc3390", []_C_int{4, 2, 6, 17}},
	{"net.inet.tcp.rootonly", []_C_int{4, 2, 6, 24}},
	{"net.inet.tcp.rstppslimit", []_C_int{4, 2, 6, 12}},
	{"net.inet.tcp.sack", []_C_int{4, 2, 6, 10}},
	{"net.inet.tcp.sackholelimit", []_C_int{4, 2, 6, 20}},
	{"net.inet.tcp.slowhz", []_C_int{4, 2, 6, 5}},
	{"net.inet.tcp.stats", []_C_int{4, 2, 6, 21}},
	{"net.inet.tcp.synbucketlimit", []_C_int{4, 2, 6, 16}},
	{"net.inet.tcp.syncachelimit", []_C_int{4, 2, 6, 15}},
	{"net.inet.tcp.synhashsize", []_C_int{4, 2, 6, 25}},
	{"net.inet.tcp.synuselimit", []_C_int{4, 2, 6, 23}},
	{"net.inet.udp.baddynamic", []_C_int{4, 2, 17, 2}},
	{"net.inet.udp.checksum", []_C_int{4, 2, 17, 1}},
	{"net.inet.udp.recvspace", []_C_int{4, 2, 17, 3}},
	{"net.inet.udp.rootonly", []_C_int{4, 2, 17, 6}},
	{"net.inet.udp.sendspace", []_C_int{4, 2, 17, 4}},
	{"net.inet.udp.stats", []_C_int{4, 2, 17, 5}},
	{"net.inet6.divert.recvspace", []_C_int{4, 24, 86, 1}},
	{"net.inet6.divert.sendspace", []_C_int{4, 24, 86, 2}},
	{"net.inet6.divert.stats", []_C_int{4, 24, 86, 3}},
	{"net.inet6.icmp6.errppslimit", []_C_int{4, 24, 30, 14}},
	{"net.inet6.icmp6.mtudisc_hiwat", []_C_int{4, 24, 30, 16}},
	{"net.inet6.icmp6.mtudisc_lowat", []_C_int{4, 24, 30, 17}},
	{"net.inet6.icmp6.nd6_debug", []_C_int{4, 24, 30, 18}},
	{"net.inet6.icmp6.nd6_delay", []_C_int{4, 24, 30, 8}},
	{"net.inet6.icmp6.nd6_maxnudhint", []_C_int{4, 24, 30, 15}},
	{"net.inet6.icmp6.nd6_mmaxtries", []_C_int{4, 24, 30, 10}},
	{"net.inet6.icmp6.nd6_umaxtries", []_C_int{4, 24, 30, 9}},
	{"net.inet6.icmp6.redirtimeout", []_C_int{4, 24, 30, 3}},
	{"net.inet6.ip6.auto_flowlabel", []_C_int{4, 24, 17, 17}},
	{"net.inet6.ip6.dad_count", []_C_int{4, 24, 17, 16}},
	{"net.inet6.ip6.dad_pending", []_C_int{4, 24, 17, 49}},
	{"net.inet6.ip6.defmcasthlim", []_C_int{4, 24, 17, 18}},
	{"net.inet6.ip6.forwarding", []_C_int{4, 24, 17, 1}},
	{"net.inet6.ip6.forwsrcrt", []_C_int{4, 24, 17, 5}},
	{"net.inet6.ip6.hdrnestlimit", []_C_int{4, 24, 17, 15}},
	{"net.inet6.ip6.hlim", []_C_int{4, 24, 17, 3}},
	{"net.inet6.ip6.log_interval", []_C_int{4, 24, 17, 14}},
	{"net.inet6.ip6.maxdynroutes", []_C_int{4, 24, 17, 48}},
	{"net.inet6.ip6.maxfragpackets", []_C_int{4, 24, 17, 9}},
	{"net.inet6.ip6.maxfrags", []_C_int{4, 24, 17, 41}},
	{"net.inet6.ip6.mforwarding", []_C_int{4, 24, 17, 42}},
	{"net.inet6.ip6.mrtmfc", []_C_int{4, 24, 17, 53}},
	{"net.inet6.ip6.mrtmif", []_C_int{4, 24, 17, 52}},
	{"net.inet6.ip6.mrtproto", []_C_int{4, 24, 17, 8}},
	{"net.inet6.ip6.mtudisctimeout", []_C_int{4, 24, 17, 50}},
	{"net.inet6.ip6.multicast_mtudisc", []_C_int{4, 24, 17, 44}},
	{"net.inet6.ip6.multipath", []_C_int{4, 24, 17, 43}},
	{"net.inet6.ip6.neighborgcthresh", []_C_int{4, 24, 17, 45}},
	{"net.inet6.ip6.redirect", []_C_int{4, 24, 17, 2}},
	{"net.inet6.ip6.soiikey", []_C_int{4, 24, 17, 54}},
	{"net.inet6.ip6.sourcecheck", []_C_int{4, 24, 17, 10}},
	{"net.inet6.ip6.sourcecheck_logint", []_C_int{4, 24, 17, 11}},
	{"net.inet6.ip6.use_deprecated", []_C_int{4, 24, 17, 21}},
	{"net.key.sadb_dump", []_C_int{4, 30, 1}},
	{"net.key.spd_dump", []_C_int{4, 30, 2}},
	{"net.mpls.ifq.congestion", []_C_int{4, 33, 3, 4}},
	{"net.mpls.ifq.drops", []_C_int{4, 33, 3, 3}},
	{"net.mpls.ifq.len", []_C_int{4, 33, 3, 1}},
	{"net.mpls.ifq.maxlen", []_C_int{4, 33, 3, 2}},
	{"net.mpls.mapttl_ip", []_C_int{4, 33, 5}},
	{"net.mpls.mapttl_ip6", []_C_int{4, 33, 6}},
	{"net.mpls.ttl", []_C_int{4, 33, 2}},
	{"net.pflow.stats", []_C_int{4, 34, 1}},
	{"net.pipex.enable", []_C_int{4, 35, 1}},
	{"vm.anonmin", []_C_int{2, 7}},
	{"vm.loadavg", []_C_int{2, 2}},
	{"vm.malloc_conf", []_C_int{2, 12}},
	{"vm.maxslp", []_C_int{2, 10}},
	{"vm.nkmempages", []_C_int{2, 6}},
	{"vm.psstrings", []_C_int{2, 3}},
	{"vm.swapencrypt.enable", []_C_int{2, 5, 0}},
	{"vm.swapencrypt.keyscreated", []_C_int{2, 5, 1}},
	{"vm.swapencrypt.keysdeleted", []_C_int{2, 5, 2}},
	{"vm.uspace", []_C_int{2, 11}},
	{"vm.uvmexp", []_C_int{2, 4}},
	{"vm.vmmeter", []_C_int{2, 1}},
	{"vm.vnodemin", []_C_int{2, 9}},
	{"vm.vtextmin", []_C_int{2, 8}},
}
