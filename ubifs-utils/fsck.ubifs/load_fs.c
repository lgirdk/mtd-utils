// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024, Huawei Technologies Co, Ltd.
 *
 * Authors: Zhihao Cheng <chengzhihao1@huawei.com>
 */

#include <stdio.h>
#include <stdlib.h>

#include "bitops.h"
#include "kmem.h"
#include "ubifs.h"
#include "defs.h"
#include "debug.h"
#include "key.h"
#include "misc.h"
#include "fsck.ubifs.h"

int ubifs_load_filesystem(struct ubifs_info *c)
{
	int err;
	size_t sz;

	err = init_constants_early(c);
	if (err) {
		exit_code |= FSCK_ERROR;
		return err;
	}

	err = check_volume_empty(c);
	if (err <= 0) {
		exit_code |= FSCK_ERROR;
		log_err(c, 0, "%s UBI volume!", err < 0 ? "bad" : "empty");
		return -EINVAL;
	}

	if (c->ro_media && !c->ro_mount) {
		exit_code |= FSCK_ERROR;
		log_err(c, 0, "cannot read-write on read-only media");
		return -EROFS;
	}

	err = -ENOMEM;
	c->bottom_up_buf = kmalloc_array(BOTTOM_UP_HEIGHT, sizeof(int),
					 GFP_KERNEL);
	if (!c->bottom_up_buf) {
		exit_code |= FSCK_ERROR;
		log_err(c, errno, "cannot allocate bottom_up_buf");
		goto out_free;
	}

	c->sbuf = vmalloc(c->leb_size);
	if (!c->sbuf) {
		exit_code |= FSCK_ERROR;
		log_err(c, errno, "cannot allocate sbuf");
		goto out_free;
	}

	if (!c->ro_mount) {
		c->ileb_buf = vmalloc(c->leb_size);
		if (!c->ileb_buf) {
			exit_code |= FSCK_ERROR;
			log_err(c, errno, "cannot allocate ileb_buf");
			goto out_free;
		}
	}

	c->mounting = 1;

	log_out(c, "Read superblock");
	err = ubifs_read_superblock(c);
	if (err) {
		if (test_and_clear_failure_reason_callback(c, FR_DATA_CORRUPTED))
			fix_problem(c, SB_CORRUPTED);
		exit_code |= FSCK_ERROR;
		goto out_mounting;
	}

	err = init_constants_sb(c);
	if (err) {
		exit_code |= FSCK_ERROR;
		goto out_mounting;
	}

	sz = ALIGN(c->max_idx_node_sz, c->min_io_size) * 2;
	c->cbuf = kmalloc(sz, GFP_NOFS);
	if (!c->cbuf) {
		err = -ENOMEM;
		exit_code |= FSCK_ERROR;
		log_err(c, errno, "cannot allocate cbuf");
		goto out_mounting;
	}

	err = alloc_wbufs(c);
	if (err) {
		exit_code |= FSCK_ERROR;
		log_err(c, 0, "cannot allocate wbuf");
		goto out_mounting;
	}

	c->mounting = 0;

	return 0;

out_mounting:
	c->mounting = 0;
out_free:
	kfree(c->cbuf);
	kfree(c->ileb_buf);
	kfree(c->sbuf);
	kfree(c->bottom_up_buf);
	kfree(c->sup_node);

	return err;
}

void ubifs_destroy_filesystem(struct ubifs_info *c)
{
	free_wbufs(c);

	kfree(c->cbuf);
	kfree(c->ileb_buf);
	kfree(c->sbuf);
	kfree(c->bottom_up_buf);
	kfree(c->sup_node);
}