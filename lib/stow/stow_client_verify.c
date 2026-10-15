/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Stow client peer verification
 *
 * @details Sends Hello, then Describe filtered to the expected names in groups that fit both sides' buffers, and
 * matches each description to an expected item by name.
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#include <gantry/error.h>
#include <gantry/stow/stow_client_serde.h>
#include <string.h>
#include <sys/errno.h>
#include <zcbor_decode.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
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
 * @brief Largest item ID a Describe may start from, used to size each group of names
 */
#define DESCRIBE_START_MAX UINT16_MAX

//**********************************************************
//* Static Function Declarations
//**********************************************************

static int send_verify_request(struct stow_client_peer* peer, enum stow_message_code code, uint32_t start_id);
static void send_describe(struct stow_client_peer* peer, uint32_t start_id);
static void verify_finished(struct stow_client_peer* peer);
static void describe_too_large(struct stow_client_peer* peer);
static int check_chunk(struct stow_client_peer* peer, const struct zcbor_string* chunk);

//**********************************************************
//* Static Function Definitions
//**********************************************************

/**
 * @brief Send a Hello, or a Describe of the current group of names, from the client thread
 */
static int send_verify_request(struct stow_client_peer* peer, enum stow_message_code code, uint32_t start_id)
{
    const struct slot pending = { .code = code, .internal = true };
    int64_t retry_at = INT64_MAX;
    k_spinlock_key_t key = k_spin_lock(&peer->lock);
    struct slot* slot = z_stow_client_slot_take(peer, &pending, &retry_at);
    k_spin_unlock(&peer->lock, key);
    if (slot == NULL)
    {
        return -EAGAIN;
    }

    struct net_buf* buf = NULL;
    zcbor_state_t encoder[ZCBOR_STATES];
    int ret = stow_wire_message_alloc(peer->transport, peer->info.max_message, &buf, encoder, ARRAY_SIZE(encoder));
    if (ret != SUCCESS)
    {
        (void)z_stow_client_slot_cancel(peer, slot, slot->seq);
        return ret;
    }

    ret = (code == STOW_MSG_HELLO)
              ? stow_client_serde_encode_start(encoder, STOW_MSG_HELLO, slot->tag, 0U)
              : stow_client_serde_encode_describe(
                    encoder, slot->tag, start_id, peer_item(peer, peer->verify.start), peer->verify.count);
    if (ret != SUCCESS)
    {
        net_buf_unref(buf);
        (void)z_stow_client_slot_cancel(peer, slot, slot->seq);
        return ret;
    }
    stow_wire_message_end(buf, encoder);
    return z_stow_client_slot_send(peer, slot, buf);
}

/**
 * @brief Describe the current group of names from @p start_id, or start the next group when none is being read
 *
 * @details Names are grouped so each Describe fits both the peer's limit and the client's own buffers.
 */
static void send_describe(struct stow_client_peer* peer, uint32_t start_id)
{
    uint16_t item_count = peer_item_count(peer);
    while (peer->verify.count == 0U)
    {
        if (peer->verify.start >= item_count)
        {
            verify_finished(peer);
            return;
        }

        const struct stow_transport* transport = peer->transport;
        size_t budget = MIN(
            peer->info.max_message, transport->pool->alloc->max_alloc_size - transport->headroom - transport->tailroom);
        size_t fit = stow_client_serde_describe_fit(
            budget, DESCRIBE_START_MAX, peer_item(peer, peer->verify.start), item_count - peer->verify.start);
        peer->verify.count = (uint16_t)((peer->verify.start < peer->verify.singly_until) ? MIN(fit, 1U) : fit);
        if (peer->verify.count == 0U)
        {
            LOG_ERR("Item %s cannot be named in a Describe request", peer_item(peer, peer->verify.start)->name);
            peer->items[peer->verify.start].availability = STOW_CLIENT_INCOMPATIBLE;
            peer->verify.start++;
        }
    }

    peer->verify.describe_from = start_id;
    int ret = send_verify_request(peer, STOW_MSG_DESCRIBE, start_id);
    if (ret != SUCCESS)
    {
        LOG_ERR("Describe to session %u failed (%d)", peer->session_id, ret);
        z_stow_client_verify_failed(peer, ret);
    }
}

static void verify_finished(struct stow_client_peer* peer)
{
    k_spinlock_key_t key = k_spin_lock(&peer->lock);
    peer->state = PEER_READY;
    k_spin_unlock(&peer->lock, key);

    CALL_HOOKS(connected, peer, SUCCESS);
}

/**
 * @brief Continue verification after a Describe answered TOO_LARGE
 *
 * @details The server names an item whose description fits no response, but the client cannot tell which of its
 * names that is. Each name of the group is described on its own instead, and a name answered TOO_LARGE on its own is
 * incompatible.
 */
static void describe_too_large(struct stow_client_peer* peer)
{
    if (peer->verify.count == 1U)
    {
        LOG_WRN("Item %s is too large to describe", peer_item(peer, peer->verify.start)->name);
        peer->items[peer->verify.start].availability = STOW_CLIENT_INCOMPATIBLE;
        peer->verify.start++;
    }
    else
    {
        peer->verify.singly_until = peer->verify.start + peer->verify.count;
    }
    peer->verify.count = 0U;
    send_describe(peer, 0U);
}

/**
 * @brief Check every item description in a Describe chunk against the expected items
 *
 * @return SUCCESS, -ENOMSG for a malformed description, or -ENOBUFS when no memory is free to check one
 */
static int check_chunk(struct stow_client_peer* peer, const struct zcbor_string* chunk)
{
    zcbor_state_t decoder[ZCBOR_STATES];
    zcbor_new_decode_state(decoder, ARRAY_SIZE(decoder), chunk->value, chunk->len, ZCBOR_MAX_ELEM_COUNT, NULL, 0);

    while (decoder[0].payload < decoder[0].payload_end)
    {
        struct stow_client_serde_description description;
        if (stow_client_serde_decode_description(decoder, &description) != SUCCESS)
        {
            LOG_WRN("Malformed description from session %u", peer->session_id);
            return -ENOMSG;
        }

        for (uint16_t id = 0; id < peer_item_count(peer); id++)
        {
            const struct stow_client_item* item = peer_item(peer, id);
            if (strlen(item->name) != description.name.len ||
                memcmp(item->name, description.name.value, description.name.len) != 0)
            {
                continue;
            }

            int ret = (description.id <= UINT16_MAX) ? stow_client_serde_check(item, &description) : -EINVAL;
            if (ret == -ENOMEM)
            {
                LOG_ERR("No memory to check item %s", item->name);
                return -ENOBUFS;
            }
            peer->items[id].availability = (ret == SUCCESS) ? STOW_CLIENT_AVAILABLE : STOW_CLIENT_INCOMPATIBLE;
            peer->items[id].remote_id = (uint16_t)description.id;
            break;
        }
    }
    return SUCCESS;
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

void z_stow_client_verify_start(struct stow_client_peer* peer)
{
    for (uint16_t id = 0; id < peer_item_count(peer); id++)
    {
        peer->items[id] = (struct item_state){ .availability = STOW_CLIENT_MISSING };
    }
    peer->info = (struct stow_client_peer_info){ 0 };
    peer->verify.start = 0U;
    peer->verify.count = 0U;
    peer->verify.singly_until = 0U;
    peer->verify.describe_from = 0U;

    k_spinlock_key_t key = k_spin_lock(&peer->lock);
    peer->state = PEER_VERIFYING;
    k_spin_unlock(&peer->lock, key);

    int ret = send_verify_request(peer, STOW_MSG_HELLO, 0U);
    if (ret != SUCCESS)
    {
        z_stow_client_verify_failed(peer, ret);
    }
}

void z_stow_client_verify_response(
    struct stow_client_peer* peer, enum stow_message_code request_code, uint32_t code, zcbor_state_t* decoder)
{
    if (code == STOW_MSG_ERROR)
    {
        uint32_t index = 0;
        int err = stow_client_serde_decode_error(decoder, &index);
        if (request_code == STOW_MSG_DESCRIBE && err == -E2BIG)
        {
            describe_too_large(peer);
            return;
        }
        z_stow_client_verify_failed(peer, err);
        return;
    }

    if (request_code == STOW_MSG_HELLO)
    {
        struct stow_client_serde_hello hello;
        int ret = (code == STOW_MSG_HELLO_RESPONSE) ? stow_client_serde_decode_hello(decoder, &hello) : -ENOMSG;
        if (ret != SUCCESS)
        {
            z_stow_client_verify_failed(peer, (ret == -EBADMSG) ? -ENOMSG : ret);
            return;
        }
        peer->info.max_message = hello.max_message;
        peer->info.roles = hello.roles;
        send_describe(peer, 0U);
        return;
    }

    uint32_t next_id = 0;
    struct zcbor_string chunk;
    int ret =
        (code == STOW_MSG_DESCRIBE_RESPONSE && stow_client_serde_decode_describe(decoder, &next_id, &chunk) == SUCCESS)
            ? check_chunk(peer, &chunk)
            : -ENOMSG;
    if (ret == SUCCESS && next_id != STOW_SERDE_ABSENT && next_id <= peer->verify.describe_from)
    {
        ret = -ENOMSG;
    }
    if (ret != SUCCESS)
    {
        z_stow_client_verify_failed(peer, ret);
        return;
    }
    if (next_id == STOW_SERDE_ABSENT)
    {
        peer->verify.start += peer->verify.count;
        peer->verify.count = 0U;
        next_id = 0U;
    }
    send_describe(peer, next_id);
}

void z_stow_client_verify_failed(struct stow_client_peer* peer, int err)
{
    if (peer->state != PEER_VERIFYING)
    {
        return;
    }

    z_stow_client_slot_fail_all(peer);

    k_spinlock_key_t key = k_spin_lock(&peer->lock);
    peer->state = PEER_FAILED;
    k_spin_unlock(&peer->lock, key);

    LOG_WRN("Session %u could not be verified (%d)", peer->session_id, err);
    CALL_HOOKS(connected, peer, err);
}
