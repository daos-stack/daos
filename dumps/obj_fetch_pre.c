vos_obj_fetch -> vos_obj_fetch_ex
        vos_fetch_begin
                vos_ioc_create(…, &ioc)
                        vos_ioc_reserve_init (quick return)
                        vos_ts_set_allocate
                        bioc = vos_data_ioctxt (return vp->vp_dummy_ioctxt;)
                        ioc->ic_biod = bio_iod_alloc(bioc, …)
                        dcs_csum_info_list_init
                        for (i = 0; i < iod_nr; i++) {
                                bsgl = bio_iod_sgl(ioc->ic_biod, i);
                                bio_sgl_init(bsgl, iov_nr);
                vos_dth_set
                vos_ts_set_add(ioc->ic_ts_set, ioc->ic_cont->vc_ts_idx, NULL, 0); (if (!vos_ts_in_tx(ts_set)) return 0;)
                occ = vos_obj_cache_current
                        vos_obj_cache_get (return vos_tls_get(standalone)->vtl_ocache;)
                vos_obj_hold(occ, …)
                        daos_lru_ref_hold(lcache = occ, …)
                                link = d_hash_rec_find(&lcache->dlc_htable, key, key_size);
                                        idx = ch_key_hash(htable, key, ksize);
                                        bucket = &htable->ht_buckets[idx];
                                        ch_bucket_lock(htable, idx, !is_lru);
                                        link = ch_rec_find(...)
                                        ch_bucket_unlock(htable, idx, !is_lru);
                                /* link == NULL */
                        if (rc == -DER_NONEXIST) {
                                D_ASSERT(obj_local.obj_cont == NULL); /* static __thread struct vos_object obj_local = {0}; */
                                obj = &obj_local;
                                init_object(obj, oid, cont);
                                        vos_cont_addref
                                                cont_addref
                                                        d_uhash_link_addref
                                                                d_hash_rec_addref
                                                                        ch_rec_addref
                                                                                uh_op_rec_addref
                                                                                        ulink = link2ulink(link);
                                                                                        rl_op_addref
                                                                                                atomic_fetch_add_relaxed
                                        vos_ilog_fetch_init(&obj->obj_ilog_info)
                                                ilog_fetch_init
                        obj->obj_sync_epoch = 0;
                        vos_oi_find(cont, oid, &obj->obj_df, ts_set);
                                dbtree_fetch(toh = cont->vc_btr_hdl, opc = BTR_PROBE_EQ, intent = DAOS_INTENT_DEFAULT, &key_iov, NULL, &val_iov);
                                        tcx = btr_hdl2tcx(toh);
                                        btr_verify_key(tcx, key);
                                        rc = btr_probe_key(tcx, opc, intent, key);
                                                btr_hkey_gen
                                                        oi_hkey_gen
                                                btr_probe(tcx, probe_opc, intent, key, hkey);
                                                        if (btr_root_empty(tcx)) { /* empty tree */
                                                                rc = PROBE_RC_NONE;
                                        case PROBE_RC_NONE:
                                                D_DEBUG(DB_TRACE, "Key does not exist.\n");
                                tmprc = vos_ilog_ts_add(ts_set, ilog, &oid, sizeof(oid)); /* if (!vos_ts_in_tx(ts_set)) return 0;*/
                        if (rc == -DER_NONEXIST) {
                        vos_obj_release(occ, obj, true);
                                if (obj == &obj_local) {
                                        clean_object(obj);
                                                vos_ilog_fetch_finish(&obj->obj_ilog_info);
                                                        ilog_fetch_finish(&info->ii_entries);
                                                vos_cont_decref(obj->obj_cont);
                                                obj_tree_fini /* close btree for an object */
                                                        /* !daos_handle_is_valid(obj->obj_toh) */
                                                memset(obj, 0, sizeof(*obj));
                stop_check(ioc, VOS_COND_FETCH_MASK | VOS_OF_COND_PER_AKEY, NULL, &rc, false)
                        if (ioc->ic_ts_set == NULL) {
                                *rc = 0
                if (rc == 0)
                        /* ioc->ic_obj == NULL */
                        for (i = 0; i < iod_nr; i++)
                                iod_empty_sgl(ioc, i);
                *ioh = vos_ioc2ioh(ioc);
                vos_dth_set(NULL, ioc->ic_cont->vc_pool->vp_sysdb);
                if (rc == -DER_NONEXIST || rc == 0) {
                        vos_fetch_add_missing(ioc->ic_ts_set, dkey, iod_nr, iods);
                                vos_ts_add_missing(ts_set, dkey, iod_nr, &ad); /* if (!vos_ts_in_tx(ts_set) || dkey == NULL) return; */
                        vos_ts_set_update(ioc->ic_ts_set, ioc->ic_epr.epr_hi); /* if (!vos_ts_in_tx(ts_set)) return; */
        *ioc = vos_ioh2ioc(ioh);
        for (i = 0; i < iod_nr; i++) {
                /* Inform caller the nonexistent of object/key */
                if (bsgl->bs_nr_out == 0) {
                        for (j = 0; j < sgl->sg_nr; j++)
                                sgl->sg_iovs[j].iov_len = 0;
        vos_obj_copy(ioc, sgls, iod_nr);
                bio_iod_prep(ioc->ic_biod, type = BIO_CHK_TYPE_IO, NULL, 0);
                        iod_prep_internal(biod, type, bulk_ctxt, bulk_perm);
                                iod_map_iovs(biod, arg);
                                        bdb = iod_dma_buf(biod);
                                        iod_fifo_in(biod, bdb);
                                        iterate_biov(biod, arg ? bulk_map_one : dma_map_one, data = arg);
                                                for (i = 0; i < biod->bd_sgl_cnt; i++) {
                                                        /* data == NULL */
                                                        /* bsgl->bs_nr_out == 0 */
                                        biod->bd_buffer_prep = 1;
                                        iod_fifo_out(biod, bdb); /* if (!biod->bd_in_fifo) return;*/
                        /* biod->bd_rsrvd.brd_rg_cnt == 0 */
                bio_iod_copy(ioc->ic_biod, sgls, sgl_nr);
                        iterate_biov(biod, copy_one, data = &arg);
                                for (i = 0; i < biod->bd_sgl_cnt; i++) {
                                        if (data != NULL) {
                                                        if (cb_fn == copy_one) {
                                                                        /* biod->bd_type == BIO_IOD_TYPE_FETCH */
                                                                        arg->ca_sgls[i].sg_nr_out = 0;
                                        /* bsgl->bs_nr_out == 0 */
                bio_iod_post(ioc->ic_biod, rc);
                        if (biod->bd_rsrvd.brd_rg_cnt == 0) {
                                iod_release_buffer(biod);
                                        bulk_iod_release(biod); /* biod->bd_bulk_hdls == NULL */
                                        if (rsrvd_dma->brd_rg_cnt == 0) {
                                                biod->bd_buffer_prep = 0;
                        return biod->bd_result; /* 0 */
        vos_fetch_end(ioh, NULL, rc);
                ioc = vos_ioh2ioc(ioh);
                vos_ioc_destroy(ioc, false);
                        if (ioc->ic_biod != NULL)
                                bio_iod_free(ioc->ic_biod);
                                        for (i = 0; i < biod->bd_sgl_cnt; i++)
                                                bio_sgl_fini
                        dcs_csum_info_list_fini(&ioc->ic_csum_list);
                        /* ioc->ic_obj == NULL */
                        vos_ioc_reserve_fini(ioc);
                                /* ioc->ic_rsrvd_scm == NULL */
                        vos_ilog_fetch_finish(&ioc->ic_dkey_info);
                                ilog_fetch_finish(&info->ii_entries);
                        vos_ilog_fetch_finish(&ioc->ic_akey_info);
                                ilog_fetch_finish(&info->ii_entries);
                        vos_cont_decref(ioc->ic_cont);
                        vos_ts_set_free(ioc->ic_ts_set); /* D_FREE(ts_set) */

