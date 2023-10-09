vos_obj_fetch -> vos_obj_fetch_ex
        vos_fetch_begin
                vos_ioc_create(…, &ioc)
                        vos_ioc_reserve_init /* if (!ioc->ic_update) return 0; */
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
                                        /* !!! */
                                        if (link != NULL)
                                                ch_rec_addref(htable, link);
                                        ch_bucket_unlock(htable, idx, !is_lru);
                                if (link != NULL) {
                                        llink = link2llink(link);
                                         if (!d_list_empty(&llink->ll_qlink))
                                                d_list_del_init(&llink->ll_qlink);
                        /* rc == 0 */
                        /** Object is in cache */
                        obj = container_of(lret, struct vos_object, obj_llink);
                        if (obj->obj_df) { /* Persistent memory address of the object */
                                tmprc = vos_ilog_ts_add(ts_set, &obj->obj_df->vo_ilog, &oid, sizeof(oid)); /* if (!vos_ts_in_tx(ts_set)) return 0;*/
                        if (!create) {
                                vos_ilog_fetch
                                        vos_ilog_fetch_internal(umm, coh, intent, ilog, &epr, bound, has_cond, punched, parent, info);
                                                vos_ilog_desc_cbs_init(&cbs, coh);
                                                ilog_fetch(umm, ilog, &cbs, intent, has_cond, &info->ii_entries);
                                                        ilog_fetch_cached(umm, root, cbs, intent, has_cond, entries)
                                                                return false
                                                /* !ilog_empty(root)) */
                                                ilog_log2cache(lctx, &cache);
                                                prepare_entries(entries, &cache);
                                                        entries->ie_ids = cache->ac_entries;
                                                for (i = 0; i < cache.ac_nr; i++) {
                                                        id = &cache.ac_entries[i];
                                                        status = ilog_status_get(lctx, id, intent, retry);
                                                                vos_ilog_status_get /* cbs->dc_log_status_cb */
                                                                        rc = vos_dtx_check_availability(coh, tx_id, epoch, intent, DTX_RT_ILOG, retry);
                                                                                dth = vos_dth_get(cont->vc_pool->vp_sysdb);
                                                                                if (dtx_is_committed(entry, cont, epoch))
                                                                        /* rc == ALB_AVAILABLE_CLEAN */
                                                        /* status == ILOG_COMMITTED */
                                                        entries->ie_info[entries->ie_num_entries].ii_removed = 0;
                                                        entries->ie_info[entries->ie_num_entries++].ii_status = status;
                                                /* entries->ie_num_entries == 1 */
                                                priv->ip_rc = rc; /* 0 */
                                                vos_parse_ilog(info, epr, bound, &punch);
                                                        ilog_foreach_entry_reverse(&info->ii_entries, &entry) {
                                                                /* entry.ie_status != ILOG_REMOVED */
                                                                /* entry.ie_status == ILOG_COMMITTED */
                                                                /* !vos_ilog_punched(&entry, punch)) */
                                                                entry_epc = entry.ie_id.id_epoch; /* 5 */
                                                                if (entry_epc > epr->epr_hi) { /* {epr_lo = 0, epr_hi = 6} */
                                                                /* !vos_ilog_punch_covered(&entry, &info->ii_prior_any_punch) */
                                                                if (entry.ie_id.id_epoch > info->ii_uncommitted) /* 5 > 0 */
                                                                        info->ii_uncommitted = 0;
                                                                /* !ilog_has_punch(&entry) */
                                                                info->ii_create = entry.ie_id.id_epoch; /* 5 */
                                                        /* epr->epr_lo == 0 */
                                                        if (vos_epc_punched(info->ii_prior_punch.pr_epc, info->ii_prior_punch.pr_minor_epc, punch))
                                                                info->ii_prior_punch = *punch;
                                                        if (vos_epc_punched(info->ii_prior_any_punch.pr_epc, info->ii_prior_any_punch.pr_minor_epc, punch))
                                                                info->ii_prior_any_punch = *punch;
                                /* rc == 0 */
                                vos_ilog_check(&obj->obj_ilog_info, epr, epr, visible_only);
                                        if (visible_only) {
                                                if (epr_out && epr_out->epr_lo < info->ii_create)
                                                        epr_out->epr_lo = info->ii_create;
                                /* rc == 0 */
                                if (obj->obj_df != NULL)
                                        obj->obj_sync_epoch = obj->obj_df->vo_sync; /* 0 */
                                /* intent == DAOS_INTENT_DEFAULT */
                                *obj_p = obj;
                        if (stop_check(ioc, VOS_COND_FETCH_MASK | VOS_OF_COND_PER_AKEY, NULL, &rc, false)) { /* if (*rc == 0) return false; */
                        /* dkey->iov_len == 6 */
                        dkey_fetch(ioc, dkey);
                                obj_tree_init(obj);
                                /* rc == 0 */
                                key_tree_prepare(tclass = VOS_BTR_DKEY, krecp = &krec, sub_toh = &toh)
                                        vos_kh_clear /* reset the saved hash */
                                        if (krecp != NULL)
                                                *krecp = NULL;
                                        dbtree_fetch(toh, BTR_PROBE_EQ, intent, key, NULL, &riov);
                                                tcx = btr_hdl2tcx(toh);
                                                btr_verify_key(tcx, key);
                                                rc = btr_probe_key(tcx, opc, intent, key);
                                                /* rc == PROBE_RC_OK */
                                                btr_trace2rec(tcx, tcx->tc_depth - 1);
                                                        btr_node_rec_at(tcx, trace->tr_node, trace->tr_at);
                                                btr_rec_fetch(tcx, rec, key_out, val_out);
                                        /* rc == 0 */
                                        krec = rbund.rb_krec;
                                        ilog = &krec->kr_ilog;
                                        tmprc = vos_ilog_ts_add(ts_set, ilog, key->iov_buf, (int)key->iov_len); /* if (!vos_ts_in_tx(ts_set)) return 0;*/
                                        if (sub_toh)
                                                tree_open_create(sub_toh)
                                                        vos_evt_desc_cbs_init(&cbs, pool, coh);
                                                        if (krec->kr_bmap & expected_flag) {
                                                                /* !(flags & SUBTR_EVT) */
                                                                        dbtree_open_inplace_ex(&krec->kr_btr, uma, coh, pool, sub_toh);
                                                                                btr_context_create(BTR_ROOT_NULL, root, -1, -1, -1, uma, coh, priv, &tcx);
                                                                                /* rc == 0 */
                                                                                *toh = btr_tcx2hdl(tcx);
                                                                        /* rc == 0 */
                                        if (krecp != NULL)
                                                *krecp = krec;
                                stop_check
                                        return
                                key_ilog_check(ioc, krec, &obj->obj_ilog_info, &ioc->ic_epr, &ioc->ic_dkey_info, has_cond);
                                /* rc == 0 */
                                stop_check
/* ... */