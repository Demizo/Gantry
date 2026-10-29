/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Stow client request slots and tags
 *
 * @details Each peer has a fixed set of slots for pending requests. Tags rotate across the whole client, so a late
 * response to a closed session rarely finds its tag in use again, and a timed-out request's tag is quarantined, so a
 * late response is not taken for a newer request.
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#include <gantry/error.h>
#include <gantry/memory.h>
#include <sys/errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "stow_client_internal.h"

/**
 * @brief Logger for module
 */
LOG_MODULE_DECLARE(stow_client, CONFIG_STOW_CLIENT_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

/**
 * @brief How many timeouts a timed-out request's tag waits for a late response before it is reused
 */
#define QUARANTINE_TIMEOUTS 3

//**********************************************************
//* Static Variable Definitions
//**********************************************************

/**
 * @brief Tag every peer tries first, so tags rotate across the client
 */
static atomic_t next_tag;

//**********************************************************
//* Static Function Declarations
//**********************************************************

static int alloc_tag(struct stow_client_peer* peer, int64_t now, int64_t* retry_at);
static void detach_slot(struct stow_client_peer* peer, struct slot* slot, void** ids_out, event_t** event_out);
static void release_detached(struct stow_client_peer* peer, void* ids, event_t* event);
static void free_slot(struct stow_client_peer* peer, struct slot* slot);
static void mark_sent(struct stow_client_peer* peer, struct slot* slot);

//**********************************************************
//* Static Function Definitions
//**********************************************************

/**
 * @brief Take the next free tag, rotating through them
 *
 * @note Call with the peer locked.
 *
 * @param peer The peer
 * @param now Uptime in ms
 * @param[out] retry_at When no tag is free, uptime in ms when the first quarantined tag frees
 *
 * @return The tag, or -1 when every tag is pending or quarantined
 */
static int alloc_tag(struct stow_client_peer* peer, int64_t now, int64_t* retry_at)
{
    *retry_at = INT64_MAX;
    atomic_val_t first = atomic_get(&next_tag);
    for (uint32_t i = 0; i < TAG_COUNT; i++)
    {
        uint8_t tag = (uint8_t)((first + i) % TAG_COUNT);
        if ((peer->slots.tags_busy & BIT(tag)) != 0U)
        {
            continue;
        }
        if ((peer->slots.quarantined & BIT(tag)) != 0U)
        {
            if (now < peer->slots.quarantine_until[tag])
            {
                *retry_at = MIN(*retry_at, peer->slots.quarantine_until[tag]);
                continue;
            }
            peer->slots.quarantined &= ~BIT(tag);
        }
        peer->slots.tags_busy |= BIT(tag);
        (void)atomic_cas(&next_tag, first, (atomic_val_t)((tag + 1U) % TAG_COUNT));
        return tag;
    }
    return -1;
}

/**
 * @brief Free a slot and its tag, handing over its copy of a Get's IDs and its result event
 *
 * @note Call with the peer locked.
 */
static void detach_slot(struct stow_client_peer* peer, struct slot* slot, void** ids_out, event_t** event_out)
{
    *ids_out = slot->ids;
    *event_out = slot->event;
    slot->ids = NULL;
    slot->event = NULL;
    slot->state = SLOT_FREE;
    peer->slots.tags_busy &= ~BIT(slot->tag);
}

/**
 * @brief Release what a freed slot held, and wake a caller waiting for a slot
 */
static void release_detached(struct stow_client_peer* peer, void* ids, event_t* event)
{
    MEM_UNREF(&ids);
    EVENT_UNREF(&event);
    k_sem_give(&peer->slot_free);
}

/**
 * @brief Free a slot, its tag, its copy of a Get's IDs and any result event still in it
 */
static void free_slot(struct stow_client_peer* peer, struct slot* slot)
{
    void* ids = NULL;
    event_t* event = NULL;
    k_spinlock_key_t key = k_spin_lock(&peer->lock);
    detach_slot(peer, slot, &ids, &event);
    k_spin_unlock(&peer->lock, key);
    release_detached(peer, ids, event);
}

static void mark_sent(struct stow_client_peer* peer, struct slot* slot)
{
    k_spinlock_key_t key = k_spin_lock(&peer->lock);
    slot->state = SLOT_SENT;
    slot->deadline = k_uptime_get() + peer->timeout_ms;
    k_spin_unlock(&peer->lock, key);
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

struct slot* z_stow_client_slot_take(struct stow_client_peer* peer, const struct slot* pending, int64_t* retry_at)
{
    *retry_at = INT64_MAX;
    for (size_t i = 0; i < ARRAY_SIZE(peer->slots.entries); i++)
    {
        struct slot* slot = &peer->slots.entries[i];
        if (slot->state != SLOT_FREE)
        {
            continue;
        }

        int tag = alloc_tag(peer, k_uptime_get(), retry_at);
        if (tag < 0)
        {
            return NULL;
        }
        *slot = *pending;
        slot->state = SLOT_QUEUED;
        slot->tag = (uint8_t)tag;
        slot->seq = ++peer->slots.seq;
        return slot;
    }
    return NULL;
}

bool z_stow_client_slot_any_free(const struct stow_client_peer* peer)
{
    for (size_t i = 0; i < ARRAY_SIZE(peer->slots.entries); i++)
    {
        if (peer->slots.entries[i].state == SLOT_FREE)
        {
            return true;
        }
    }
    return false;
}

bool z_stow_client_slot_cancel(struct stow_client_peer* peer, struct slot* slot, uint32_t seq)
{
    void* ids = NULL;
    event_t* event = NULL;
    k_spinlock_key_t key = k_spin_lock(&peer->lock);
    bool current = (slot->state == SLOT_QUEUED && slot->seq == seq);
    if (current)
    {
        detach_slot(peer, slot, &ids, &event);
    }
    k_spin_unlock(&peer->lock, key);

    if (current)
    {
        release_detached(peer, ids, event);
    }
    return current;
}

void z_stow_client_slot_finish(struct stow_client_peer* peer, struct slot* slot, int err, uint16_t index)
{
    stow_client_cb_t cb = slot->cb;
    void* user_data = slot->user_data;
    struct sync_call* sync = slot->sync;
    event_t* event = slot->event;
    slot->event = NULL;
    free_slot(peer, slot);

    if (sync != NULL)
    {
        sync->err = err;
        k_sem_give(&sync->done);
    }
    if (event != NULL)
    {
        struct stow_client_result_payload* result = (struct stow_client_result_payload*)event->data.buf;
        result->err = err;
        result->index = index;
        cb(event, user_data);
        EVENT_UNREF(&event);
    }
}

void z_stow_client_slot_fail(struct stow_client_peer* peer, struct slot* slot, int err)
{
    z_stow_client_slot_finish(peer, slot, err, STOW_CLIENT_NO_INDEX);
}

void z_stow_client_slot_fail_all(struct stow_client_peer* peer)
{
    for (size_t i = 0; i < ARRAY_SIZE(peer->slots.entries); i++)
    {
        struct slot* slot = &peer->slots.entries[i];
        k_spinlock_key_t key = k_spin_lock(&peer->lock);
        bool pending = (slot->state != SLOT_FREE);
        if (pending)
        {
            slot->state = SLOT_SENT;
        }
        k_spin_unlock(&peer->lock, key);

        if (pending)
        {
            z_stow_client_slot_fail(peer, slot, -ENOTCONN);
        }
    }
}

int z_stow_client_slot_send(struct stow_client_peer* peer, struct slot* slot, struct net_buf* buf)
{
    int ret = peer->transport->send(peer->transport, peer->session_id, buf);
    if (ret != SUCCESS)
    {
        LOG_WRN("Transport refused a request to session %u (%d)", peer->session_id, ret);
        net_buf_unref(buf);
        z_stow_client_slot_fail(peer, slot, -EBUSY);
        return -EBUSY;
    }
    PASS_OWNERSHIP(buf);
    mark_sent(peer, slot);
    return SUCCESS;
}

struct slot* z_stow_client_slot_find_sent(struct stow_client_peer* peer, uint8_t tag)
{
    for (size_t i = 0; i < ARRAY_SIZE(peer->slots.entries); i++)
    {
        if (peer->slots.entries[i].state == SLOT_SENT && peer->slots.entries[i].tag == tag)
        {
            return &peer->slots.entries[i];
        }
    }
    return NULL;
}

int64_t z_stow_client_slots_deadline(const struct stow_client_peer* peer)
{
    int64_t deadline = INT64_MAX;
    for (size_t i = 0; i < ARRAY_SIZE(peer->slots.entries); i++)
    {
        const struct slot* slot = &peer->slots.entries[i];
        if (slot->state == SLOT_SENT && slot->deadline < deadline)
        {
            deadline = slot->deadline;
        }
    }
    return deadline;
}

bool z_stow_client_slots_expire(struct stow_client_peer* peer, int64_t now)
{
    bool verification = false;
    for (size_t i = 0; i < ARRAY_SIZE(peer->slots.entries); i++)
    {
        struct slot* slot = &peer->slots.entries[i];
        if (slot->state != SLOT_SENT || slot->deadline > now)
        {
            continue;
        }

        LOG_WRN("Request with tag %u to session %u timed out", slot->tag, peer->session_id);
        k_spinlock_key_t key = k_spin_lock(&peer->lock);
        peer->slots.quarantined |= BIT(slot->tag);
        peer->slots.quarantine_until[slot->tag] = now + (int64_t)QUARANTINE_TIMEOUTS * peer->timeout_ms;
        k_spin_unlock(&peer->lock, key);

        verification |= slot->internal;
        z_stow_client_slot_fail(peer, slot, -ETIMEDOUT);
    }
    return verification;
}

bool z_stow_client_tag_answered(struct stow_client_peer* peer, uint8_t tag)
{
    k_spinlock_key_t key = k_spin_lock(&peer->lock);
    bool late = (peer->slots.quarantined & BIT(tag)) != 0U;
    peer->slots.quarantined &= ~BIT(tag);
    k_spin_unlock(&peer->lock, key);
    k_sem_give(&peer->slot_free);
    return late;
}
