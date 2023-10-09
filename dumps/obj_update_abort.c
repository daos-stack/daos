vos_local_tx_begin
        vos_dtx_rsrvd_init(dth);
        pool = vos_hdl2pool(poh);
        umm  = vos_pool2umm(pool);
        vos_tx_begin(dth, umm, pool->vp_sysdb);
                umem_tx_begin(umm, vos_txd_get(is_sysdb));
                        pmem_tx_begin /* umm->umm_ops->mo_tx_begin(umm, txd); */
                                pmemobj_tx_begin(pop, NULL, TX_PARAM_CB, pmem_stage_callback, txd, TX_PARAM_NONE);
                                /* rc == 0 */
                vos_dth_set(dth, false);
vos_obj_update_ex
        vos_update_begin(coh, oid, epoch, flags, dkey, iod_nr, iods, iods_csums, 0, &ioh, dth);
                /* !dtx_is_real_handle(dth) */
                vos_check_akeys(iod_nr, iods);
                /* rc == 0 */
                vos_ioc_create(coh, oid, false, epoch, iod_nr, iods, iods_csums, flags, NULL, dedup_th, dth, &ioc);
                        vos_ilog_fetch_init(&ioc->ic_dkey_info);
                        vos_ilog_fetch_init(&ioc->ic_akey_info);
                        vos_ioc_reserve_init(ioc, dth);
                                umem_rsrvd_act_alloc(vos_ioc2umm(ioc), &ioc->ic_rsrvd_scm, total_acts);
                                /* dtx_is_valid_handle(dth) == true */
                                umem_rsrvd_act_alloc(vos_ioc2umm(ioc), &scm, total_acts);
                                dth->dth_deferred[dth->dth_deferred_cnt++] = scm;
                        cont = vos_hdl2cont(coh);
                        vos_ts_set_allocate(&ioc->ic_ts_set, vos_flags, cflags, iod_nr, dth, cont->vc_pool->vp_sysdb);
                                if (!dtx_is_valid_handle(dth) || dth->dth_local) {
		                        if ((flags & cond_mask) == 0)
			                        return 0;
                        bioc = vos_data_ioctxt(cont->vc_pool);
                        ioc->ic_biod = bio_iod_alloc(bioc, vos_ioc2umm(ioc), iod_nr, read_only ? BIO_IOD_TYPE_FETCH : BIO_IOD_TYPE_UPDATE);
                        dcs_csum_info_list_init(&ioc->ic_csum_list, iod_nr);
                        for (i = 0; i < iod_nr; i++) {
                                bsgl = bio_iod_sgl(ioc->ic_biod, i);
                                        bsgl = &biod->bd_sgls[idx];
                                bio_sgl_init(bsgl, iov_nr);
                                        return sgl->bs_iovs == NULL ? -DER_NOMEM : 0;
                        *ioc_pp = ioc;
                /* rc == 0 */
                vos_space_hold(vos_cont2pool(ioc->ic_cont), flags, dkey, iod_nr, iods, iods_csums, &ioc->ic_space_held[0]);
                        vos_space_query(pool, &vps, false);
                                umempobj_get_heapusage(pool->vp_umm.umm_pool, &scm_used);
                                        switch (ph_p->up_store.store_type) {
                                        case DAOS_MD_PMEM:
                                                pmemobj_ctl_get(pop, "stats.heap.curr_allocated", curr_allocated);
                                /* rc == 0 */
                                SCM_FREE(vps) = SCM_TOTAL(vps) - scm_used;
                                /* NVMe isn't configured for this VOS pool */
                                if (pool->vp_vea_info == NULL) {
                                        NVME_TOTAL(vps) = 0;
                                        NVME_FREE(vps) = 0;
                                        NVME_SYS(vps) = 0;
                                        return 0;
                        /* rc == 0 */
                        estimate_space(pool, dkey, iod_nr, iods, iods_csums, &space_est[0]);
                                scm += estimate_space_key(umm, dkey);
                                for (i = 0; i < iod_nr; i++) {
                                        iod = &iods[i];
                                        scm += estimate_space_key(umm, &iod->iod_name);
                                        csums = vos_csum_at(iods_csums, i);
                                        if (iod->iod_type == DAOS_IOD_SINGLE) {
                                                size = iod->iod_size;
                                                media = vos_policy_media_select(pool, iod->iod_type, size, VOS_IOS_GENERIC);
                                                if (media == DAOS_MEDIA_SCM) {
                                                        scm += vos_recx2irec_size(size, csums);
                                                /* Assume one more SV tree node created */
                                                scm += 256;
                                                continue;
                                space_est[DAOS_MEDIA_SCM] = scm;
                                space_est[DAOS_MEDIA_NVME] = nvme * VOS_BLK_SZ;
                        scm_left = SCM_FREE(&vps);
                        scm_left -= SCM_SYS(&vps);
                        scm_left -= POOL_SCM_HELD(pool);
                        space_hld[DAOS_MEDIA_SCM]       = space_est[DAOS_MEDIA_SCM];
                        space_hld[DAOS_MEDIA_NVME]      = space_est[DAOS_MEDIA_NVME];
                        POOL_SCM_HELD(pool)             += space_hld[DAOS_MEDIA_SCM];
                        POOL_NVME_HELD(pool)            += space_hld[DAOS_MEDIA_NVME];
                /* rc == 0 */
                dkey_update_begin(ioc);
                        for (i = 0; i < ioc->ic_iod_nr; i++) {
                                iod_set_cursor(ioc, i);
                                akey_update_begin(ioc);
                                        iod_csums = vos_csum_at(ioc->ic_iod_csums, ioc->ic_sgl_at);
                                        iod = &ioc->ic_iods[ioc->ic_sgl_at];
                                        for (i = 0; i < iod->iod_nr; i++) {
                                                /* iod->iod_type == DAOS_IOD_SINGLE */
                                                size = iod->iod_size;
                                                media = vos_policy_media_select(vos_cont2pool(ioc->ic_cont), iod->iod_type, size, VOS_IOS_GENERIC);
                                                        policy_io_size /* vos_policies[pool->vp_policy_desc.policy](pool, type, size); */
                                                /* media == DAOS_MEDIA_SCM (0) */
                                                if (iod->iod_type == DAOS_IOD_SINGLE) {
                                                        vos_reserve_single(ioc, media, size);
                                                                value_csum = vos_csum_at(ioc->ic_iod_csums, ioc->ic_sgl_at);
                                                                scm_size = vos_recx2irec_size(size, value_csum);
                                                                /* scm_size == 53 */
                                                                reserve_space(ioc, DAOS_MEDIA_SCM, scm_size, &off);
                                                                        if (media == DAOS_MEDIA_SCM) {
                                                                                umoff = vos_reserve_scm(ioc->ic_cont, ioc->ic_rsrvd_scm, size);
                                                                                        umoff = umem_reserve(vos_cont2umm(cont), rsrvd_scm, size);
                                                                                                if (umm->umm_ops->mo_reserve) {
                                                                                                        act_size = umem_rsrvd_item_size(umm);
                                                                                                        act = rsrvd_act->rs_actv + act_size * rsrvd_act->rs_actv_at;
                                                                                                        pmem_reserve /* off = umm->umm_ops->mo_reserve(umm, act, size, UMEM_TYPE_ANY); */
                                                                                                                return umem_id2off(umm, pmemobj_reserve(pop, (struct pobj_action *)act, size, type_num));
                                                                                                        if (!UMOFF_IS_NULL(off))
                                                                                                                rsrvd_act->rs_actv_at++;
                                                                                if (!UMOFF_IS_NULL(umoff)) {
                                                                                        ioc->ic_umoffs[ioc->ic_umoffs_cnt] = umoff;
                                                                                        ioc->ic_umoffs_cnt++;
                                                                                        *off = umoff;
                                                                                        return 0;
                                                                umoff = ioc->ic_umoffs[ioc->ic_umoffs_cnt - 1];
                                                                irec = (struct vos_irec_df *)umem_off2ptr(vos_ioc2umm(ioc), umoff);
                                                                vos_irec_init_csum(irec, value_csum);
                                                                memset(&biov, 0, sizeof(biov));
                                                                if (media == DAOS_MEDIA_SCM) {
                                                                        payload_addr = vos_irec2data(irec);
                                                                        off = umoff + (payload_addr - (char *)irec);
                                                                bio_addr_set(&biov.bi_addr, media, off);
                                                                bio_iov_set_len(&biov, size);
                                                                iod_reserve(ioc, &biov);
                                                                        bsgl = bio_iod_sgl(ioc->ic_biod, ioc->ic_sgl_at);
                /* rc == 0 */
                *ioh = vos_ioc2ioh(ioc);
                vos_obj_copy(vos_ioh2ioc(ioh), sgls, iod_nr);
                        bio_iod_prep(ioc->ic_biod, type = BIO_CHK_TYPE_IO, NULL, 0);
                                iod_prep_internal(biod, type, bulk_ctxt, bulk_perm);
                                        iod_map_iovs(biod, arg);
                                                /* biod->bd_ctxt->bic_xs_ctxt != NULL */
                                                bdb = iod_dma_buf(biod);
                                                iod_fifo_in(biod, bdb);
                                                iterate_biov(biod, arg ? bulk_map_one : dma_map_one, arg);
                                                iod_fifo_out(biod, bdb);
                                        /* All direct SCM access, no DMA buffer prepared */
	                                if (biod->bd_rsrvd.brd_rg_cnt == 0)
		                                return 0;
                        bio_iod_copy(ioc->ic_biod, sgls, sgl_nr);
                                return iterate_biov(biod, copy_one, &arg);
                                        for (i = 0; i < biod->bd_sgl_cnt; i++) {
                                                bsgl = &biod->bd_sgls[i];
                                                if (data != NULL) {
                                                        if (cb_fn == copy_one) {
                                                for (j = 0; j < bsgl->bs_nr_out; j++) {
                                                        biov = &bsgl->bs_iovs[j];
                                                        copy_one /* cb_fn(biod, biov, data); */
                                                                addr = bio_iov2req_buf(biov);
                                                                size = bio_iov2req_len(biov);
                                                                media = bio_iov2media(biov);
                                                                sgl = &arg->ca_sgls[arg->ca_sgl_idx];
                                                                while (arg->ca_iov_idx < sgl->sg_nr) {
                                                                        iov = &sgl->sg_iovs[arg->ca_iov_idx];
                                                                        /* biod->bd_type == BIO_IOD_TYPE_UPDATE */
                                                                        buf_len = iov->iov_len; /* 5 */
                                                                        /* iov->iov_buf == NULL */
                                                                        nob = min(size, buf_len - arg->ca_iov_off); /* 5 */
                                                                        /* arg->ca_size_tot == 0 */
                                                                        if (addr != NULL) {
                                                                                bio_memcpy(biod, media, addr, iov->iov_buf + arg->ca_iov_off, nob);
                                                                                        if (biod->bd_type == BIO_IOD_TYPE_UPDATE && media == DAOS_MEDIA_SCM) {
                                                                                                umem_atomic_copy(umem, media_addr, addr, n, UMEM_RESERVED_MEM);
                                                                                                        pmem_atomic_copy /* return umm->umm_ops->mo_atomic_copy(umm, dest, src, len, hint); */
                                                                                                                pmemobj_memcpy_persist(pop, dest, src, len);
                                                                                addr += nob;
                                                                                arg->ca_iov_off += nob;
                                                                                /* consumed an iov, move to the next */
                                                                                if (arg->ca_iov_off == iov->iov_len) {
                                                                                        arg->ca_iov_off = 0;
                                                                                        arg->ca_iov_idx++;
                                                                        if (size == 0)
                                                                                return 0;
                        bio_iod_post(ioc->ic_biod, rc);
                                /* No more actions for direct accessed SCM IOVs */
                                if (biod->bd_rsrvd.brd_rg_cnt == 0) {
                                        iod_release_buffer(biod);
                                        goto out;
                                /* !biod->bd_dma_issued */
                                iod_dma_completion(biod, biod->bd_result);
                                return biod->bd_result;
                vos_update_end(ioh, pm_ver, dkey, rc, NULL, dth);
                        vos_dedup_verify_fini(ioh);
                                ioc = vos_ioh2ioc(ioh);
                                if (ioc->ic_dedup_bsgls == NULL)
                                        return;
                        umem = vos_ioc2umm(ioc);
                        vos_ts_set_add(ioc->ic_ts_set, ioc->ic_cont->vc_ts_idx, NULL, 0);
                                if (!vos_ts_in_tx(ts_set))
                                        return 0;
                        vos_tx_begin(dth, umem, ioc->ic_cont->vc_pool->vp_sysdb);
                                /* dth != NULL */
                                if (dth->dth_local_tx_started) {
                                        vos_dth_set(dth, false);
                                        return 0;
                        /* dth->dth_local == true */
                        vos_obj_hold(vos_obj_cache_current(ioc->ic_cont->vc_pool->vp_sysdb), ioc->ic_cont, ioc->ic_oid, &ioc->ic_epr, ioc->ic_bound, VOS_OBJ_CREATE | VOS_OBJ_VISIBLE, DAOS_INTENT_UPDATE, &ioc->ic_obj, ioc->ic_ts_set);
                                /* create == true */
                                daos_lru_ref_hold(occ, &lkey, sizeof(lkey), create_flag, &lret);
                                        link = d_hash_rec_find(&lcache->dlc_htable, key, key_size);
                                        /* link == NULL */
                                        obj_lop_alloc /* lcache->dlc_ops->lop_alloc_ref(key, key_size, create_args, &llink); */
                                                init_object(obj, lkey->olk_oid, cont);
                                                *llink_p = &obj->obj_llink;
                                        d_hash_rec_insert(&lcache->dlc_htable, key, key_size, &llink->ll_link, true);
                                        /* rc == 0 */
                                        lcache->dlc_count++;
                                        *llink_pp = llink;
                                /* rc == 0 */
                                obj = container_of(lret, struct vos_object, obj_llink);
                                obj->obj_sync_epoch = 0;
                                vos_oi_find_alloc(cont, oid, epr->epr_hi, false, &obj->obj_df, ts_set);
                                        vos_oi_find(cont, oid, &obj, ts_set);
                                        /* rc == DER_NONEXIST */
                                        dbtree_upsert(cont->vc_btr_hdl, BTR_PROBE_EQ, DAOS_INTENT_DEFAULT, &key_iov, &val_iov, NULL);
                                        /* rc == 0 */
                                        obj = val_iov.iov_buf;
                                        vos_ilog_ts_ignore(vos_cont2umm(cont), &obj->vo_ilog);
                                        vos_ilog_ts_mark(ts_set, &obj->vo_ilog);
                                                vos_ts_set_mark_entry(ts_set, idx);
                                        /* log == false */
                                        if (rc == 0)
                                                *obj_p = obj;
                                /* rc == 0 */
                                vos_ilog_update(cont, &obj->obj_df->vo_ilog, epr, bound, NULL, &obj->obj_ilog_info, cond_mask, ts_set);
                                        vos_ilog_fetch(vos_cont2umm(cont), vos_cont2hdl(cont), DAOS_INTENT_UPDATE, ilog, epr->epr_hi, bound, has_cond, NULL, parent, info);
                                        /* rc == -DER_NONEXIST */
                                        vos_ilog_desc_cbs_init(&cbs, vos_cont2hdl(cont));
                                        ilog_open(umem = vos_cont2umm(cont), ilog, &cbs, &loh);
                                                ilog_ctx_create(umm, (struct ilog_root *)root, cbs, &lctx);
                                                        (*lctxp)->ic_umm = umm;
                                        /* rc == 0 */
                                        ilog_update(loh, &max_epr, epr->epr_hi, (dtx_is_real_handle(dth) ? dth->dth_op_seq : VOS_SUB_OP_MAX), false);
                                                id.id_update_minor_eph = minor_eph;
                                                if (epr)
                                                        range = *epr;
                                                ilog_modify(loh, &id, &range, ILOG_OP_UPDATE);
                                                        if (ilog_empty(root)) {
                                                                ilog_ptr_set(lctx, root, &tmp);
                                                                        ilog_ptr_set_full
                                                                                ilog_tx_begin(lctx);
                                                                                        /* !lctx->ic_in_txn */
                                                                                        umem_tx_begin(lctx->ic_umm, NULL);
                                                                                                pmem_tx_begin /* umm->umm_ops->mo_tx_begin(umm, txd); */
                                                                                                        pmemobj_tx_begin(pop, NULL, TX_PARAM_NONE);
                                                                                        lctx->ic_in_txn = true;
                                                                                        lctx->ic_ver_inc = true;
                                                                                umem_tx_add_ptr(lctx->ic_umm, dest, len);
                                                                                        pmem_tx_add_ptr /* umm->umm_ops->mo_tx_add_ptr(umm, ptr, size); */
                                                                                                pmemobj_tx_add_range_direct(ptr, size);
                                                                        memcpy(dest, src, len);
                                                                ilog_log_add(lctx, &root->lr_id);
                                                                        vos_ilog_add /* cbs->dc_log_add_cb(lctx->ic_umm, lctx->ic_root_off, &id->id_tx_id, id->id_epoch, cbs->dc_log_add_args); */
                                                                                vos_dtx_register_record(umm, ilog_off, DTX_RT_ILOG, tx_id);
                                                                                        if (!dtx_is_real_handle(dth)) {
                                                                                                dtx_set_committed(tx_id);
                                                                                                        *tx_lid = DTX_LID_COMMITTED;
                                                        ilog_tx_end(lctx, rc);
                                                                if (lctx->ic_ver_inc) {
                                                                        umem_tx_add_ptr(lctx->ic_umm, &lctx->ic_root->lr_magic, sizeof(lctx->ic_root->lr_magic));
                                                                                pmem_tx_add_ptr /* umm->umm_ops->mo_tx_add_ptr(umm, ptr, size); */
                                                                                        pmemobj_tx_add_range_direct(ptr, size);
                                                                        lctx->ic_root->lr_magic = ilog_ver_inc(lctx);
                                                                                magic += ILOG_VERSION_INC;
                                                                                /* This is only called when we will persist the new version so no need
                                                                                * to update the version when finishing the transaction.
                                                                                */
                                                                                lctx->ic_ver_inc = false;
                                                                        lctx->ic_in_txn = false;
                                                                        umem_tx_end(lctx->ic_umm, rc);
                                                                                umem_tx_end_ex(umm, err, NULL);
                                                                                        pmem_tx_commit /* umm->umm_ops->mo_tx_commit(umm, data); */
                                                                                                pmemobj_tx_commit();
                                                                                                rc = pmemobj_tx_end();
                                        /* rc == 0 */
                                        ilog_close(loh);
                                /* rc == 0 */
                                if (obj->obj_df != NULL)
                                        obj->obj_sync_epoch = obj->obj_df->vo_sync;
                                *obj_p = obj;
                        /* err == 0 */
                        dkey_update(ioc, pm_ver, dkey, (dtx_is_real_handle(dth) ? dth->dth_op_seq : VOS_SUB_OP_MAX));
                                obj_tree_init
                                        /* obj->obj_df->vo_tree.tr_class == 0 */ /* ID to find a registered tree class, which provides customized functions etc.*/
                                        type = daos_obj_id2type(obj->obj_df->vo_id.id_pub);
                                        /* type == DAOS_OT_MULTI_HASHED */
                                        /* daos_is_dkey_uint64_type(type) == false */
                                        /* daos_is_dkey_lexical_type(type) == false */
                                        dbtree_create_inplace_ex
                                                btr_context_create(BTR_ROOT_NULL, root, tree_class, tree_feats, tree_order, uma, coh, priv, &tcx);
                                                        tcx->tc_ref = 1;
                                                        btr_class_init(root_off, root, tree_class, &tree_feats, uma, coh, priv, &tcx->tc_tins);
                                                                umem_class_init(uma, &tins->ti_umm);
                                                        if (root == NULL || root->tr_class == 0) { /* tree creation */
                                                        btr_context_set_depth(tcx, depth);
                                                btr_tx_tree_init(tcx, root);
                                                        btr_tx_begin(tcx);
                                                                umem_tx_begin(btr_umm(tcx), NULL);
                                                                        pmem_tx_begin /* umm->umm_ops->mo_tx_begin(umm, txd); */
                                                                                pmemobj_tx_begin(pop, NULL, TX_PARAM_NONE);
                                                        btr_tree_init(tcx, root);
                                                                btr_root_init(tcx, root, true);
                                                                        if (UMOFF_IS_NULL(tins->ti_root_off) && btr_has_tx(tcx)) {
                                                                                btr_root_tx_add(tcx);
                                                                                        /* UMOFF_IS_NULL(tins->ti_root_off) == true */
                                                                                        umem_tx_add_ptr(btr_umm(tcx), tcx->tc_tins.ti_root, sizeof(struct btr_root));
                                                                                                pmem_tx_add_ptr /* umm->umm_ops->mo_tx_add_ptr(umm, ptr, size); */
                                                                                                        pmemobj_tx_add_range_direct(ptr, size);
                                                                        if (in_place) memset(root, 0, sizeof(*root));
                                                        btr_tx_end
                                                                umem_tx_commit(btr_umm(tcx));
                                                                        umem_tx_commit_ex(umm, NULL);
                                                                                pmem_tx_commit /* umm->umm_ops->mo_tx_commit(umm, data); */
                                                                                        pmemobj_tx_commit();
                                                                                        pmemobj_tx_end();
                                                *toh = btr_tcx2hdl(tcx);
                                key_tree_prepare
                                        vos_kh_clear(obj->obj_cont->vc_pool->vp_sysdb); /** reset the saved hash */
                                        tree_rec_bundle2iov(&rbund, &riov);
                                        dbtree_fetch(toh, BTR_PROBE_EQ, intent, key, NULL, &riov);
                                                btr_verify_key(tcx, key);
                                                btr_probe_key(tcx, opc, intent, key);
                                                        btr_hkey_gen(tcx, key, hkey);
                                                        btr_probe(tcx, probe_opc, intent, key, hkey);
                                                                btr_context_set_depth(tcx, tcx->tc_tins.ti_root->tr_depth);
                                                                if (btr_root_empty(tcx)) { /* empty tree */
                                                                        rc = PROBE_RC_NONE;
                                                                tcx->tc_probe_rc = rc;
                                                        switch (rc) {
                                                        case PROBE_RC_NONE:
                                                                return -DER_NONEXIST;
                                        switch (rc) {
                                        case -DER_NONEXIST:
                                                /* ilog == NULL */
                                                vos_ilog_ts_add(ts_set, ilog, key->iov_buf, (int)key->iov_len);
                                        if (rc == -DER_NONEXIST) {
                                                /* flags & SUBTR_CREATE */
                                                dbtree_upsert(toh, BTR_PROBE_BYPASS, intent, key, &riov, NULL);
                                                        btr_tx_begin(tcx);
                                                                umem_tx_begin
                                                                        pmem_tx_begin /* umm->umm_ops->mo_tx_begin(umm, txd); */
                                                                                pmemobj_tx_begin(pop, NULL, TX_PARAM_NONE);
                                                        btr_upsert(tcx, opc, intent, key, val, val_out);
                                                                switch (rc) {
                                                                case PROBE_RC_NONE:
                                                                        btr_insert(tcx, key, val, val_out);
                                                                                btr_rec_alloc(tcx, key, val, rec, val_out);
                                                                                        ktr_rec_alloc /* btr_ops(tcx)->to_rec_alloc(&tcx->tc_tins, key, val, rec, val_out); */
                                                                                                ilog_create(&tins->ti_umm, &krec->kr_ilog);
                                                                                                        ilog_ptr_set(&lctx, root, &tmp);
                                                                                                                ilog_ptr_set_full
                                                                                                                        ilog_tx_begin(lctx);
                                                                                                                                umem_tx_begin(lctx->ic_umm, NULL);
                                                                                                                                        pmem_tx_begin /* umm->umm_ops->mo_tx_begin(umm, txd); */
                                                                                                                                                pmemobj_tx_begin(pop, NULL, TX_PARAM_NONE);
                                                                                                                        umem_tx_add_ptr(lctx->ic_umm, dest, len);
                                                                                                                                pmem_tx_add_ptr /* umm->umm_ops->mo_tx_add_ptr(umm, ptr, size); */
                                                                                                                                        pmemobj_tx_add_range_direct(ptr, size);
                                                                                                                        memcpy(dest, src, len);
                                                                                                        ilog_tx_end(&lctx, rc);
                                                                                                                /* lctx->ic_ver_inc == false */
                                                                                                                umem_tx_end(lctx->ic_umm, rc);
                                                                                                                        umem_tx_end_ex(umm, err, NULL);
                                                                                                                                umem_tx_commit_ex(umm, data);
                                                                                                                                        pmem_tx_commit /* umm->umm_ops->mo_tx_commit(umm, data); */
                                                                                                                                                pmemobj_tx_commit
                                                                                                                                                pmemobj_tx_end
                                                                                                ktr_rec_store(tins, rec, key_iov, rbund);
                                                                                                        memcpy(kbuf, iov->iov_buf, iov->iov_len);
                                                                                                        krec->kr_size = iov->iov_len;
                                                                                btr_root_start(tcx, rec);
                                                                                        btr_node_alloc(tcx, &nd_off);
                                                                                                if (btr_ops(tcx)->to_node_alloc != NULL)
                                                                                                        pmem_tx_alloc /* nd_off = btr_ops(tcx)->to_node_alloc(&tcx->tc_tins, btr_node_size(tcx)); */
                                                                                                                pmemobj_tx_xalloc
                                                                                        btr_node_set(tcx, nd_off, BTR_NODE_ROOT | BTR_NODE_LEAF);
                                                                                        rec_dst = btr_node_rec_at(tcx, nd_off, 0);
                                                                                        btr_rec_copy(tcx, rec_dst, rec, 1);
                                                                                        if (btr_has_tx(tcx)) {
                                                                                                btr_root_tx_add(tcx);
                                                                                                        umem_tx_add_ptr(btr_umm(tcx), tcx->tc_tins.ti_root, sizeof(struct btr_root));
                                                                                                                pmem_tx_add_ptr /* umm->umm_ops->mo_tx_add_ptr(umm, ptr, size); */
                                                                                                                        pmemobj_tx_add_range_direct
                                                        btr_tx_end(tcx, rc);
                                                                XXX
                                                                                                                


