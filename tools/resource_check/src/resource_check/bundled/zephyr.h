/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Annotated prototypes of Zephyr APIs, read by the resource checker. This file is only scanned, never compiled or
 * included, so it needs no includes. Parameter names match Zephyr's public declarations (v4.4).
 */

/** @acquires irq return */
unsigned int irq_lock(void);

/** @releases irq key */
void irq_unlock(unsigned int key);

/** @acquires spinlock return */
k_spinlock_key_t k_spin_lock(struct k_spinlock* l);

/** @releases spinlock key */
void k_spin_unlock(struct k_spinlock* l, k_spinlock_key_t key);

/** @acquires mutex mutex on-success */
int k_mutex_lock(struct k_mutex* mutex, k_timeout_t timeout);

/** @releases mutex mutex */
int k_mutex_unlock(struct k_mutex* mutex);

/** @acquires net_buf return on-non-null */
struct net_buf* net_buf_alloc(struct net_buf_pool* pool, k_timeout_t timeout);

/** @acquires net_buf return */
struct net_buf* net_buf_ref(struct net_buf* buf);

/** @releases net_buf buf */
void net_buf_unref(struct net_buf* buf);

/** @acquires heap return on-non-null */
void* k_malloc(size_t size);

/** @releases heap ptr */
void k_free(void* ptr);

/** @acquires mem_slab mem on-success */
int k_mem_slab_alloc(struct k_mem_slab* slab, void** mem, k_timeout_t timeout);

/** @releases mem_slab mem */
void k_mem_slab_free(struct k_mem_slab* slab, void* mem);
