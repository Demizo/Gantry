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
#include <gantry/memory.h>
#include <gantry/stow/stow_msg.h>
#include <string.h>
#include <sys/errno.h>
#include <zcbor_encode.h>
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
//* Static Function Declarations
//**********************************************************

static void put_names(struct stow_client_peer* peer, struct stow_msg_writer* writer);
static int send_verify_request(struct stow_client_peer* peer, enum stow_message_code code, uint32_t start_id);
static void send_describe(struct stow_client_peer* peer, uint32_t start_id);
static void verify_finished(struct stow_client_peer* peer);
static void describe_too_large(struct stow_client_peer* peer);
static int check_item(const struct stow_client_item* item, const struct stow_msg_description* description);
static int check_descriptions(struct stow_client_peer* peer, struct stow_msg_reader* reader, uint16_t count);

//**********************************************************
//* Static Function Definitions
//**********************************************************

/**
 * @brief Put the names of the current Describe group, choosing the group when none is being read
 *
 * @details A group holds as many names as fit the peer's limit and the client's buffers; the rest go to the next group.
 */
static void put_names(struct stow_client_peer* peer, struct stow_msg_writer* writer)
{
    uint16_t start = peer->verify.start;
    uint16_t limit = peer->verify.count;
    if (limit == 0U)
    {
        limit = (start < peer->verify.singly_until) ? 1U : (uint16_t)(peer_item_count(peer) - start);
    }

    uint16_t put = 0;
    while (put < limit && stow_msg_put_name(writer, peer_item(peer, start + put)->name) == SUCCESS)
    {
        put++;
    }
    peer->verify.count = put;
}

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
    size_t budget = 0;
    int ret = stow_transport_alloc(peer->session.transport, peer->info.max_message, &buf, &budget);
    if (ret != SUCCESS)
    {
        (void)z_stow_client_slot_cancel(peer, slot, slot->seq);
        return ret;
    }

    struct stow_msg_writer writer;
    stow_msg_writer_init(&writer, net_buf_tail(buf), budget);
    bool describe = (code == STOW_MSG_DESCRIBE);
    const struct stow_msg msg = { .code = code,
                                  .tag = slot->tag,
                                  .describe = { .start = start_id, .named = describe } };
    (void)stow_msg_write(&writer, &msg);
    if (describe)
    {
        put_names(peer, &writer);
    }

    size_t len = 0;
    ret = stow_msg_finish(&writer, &len);
    if (ret != SUCCESS)
    {
        net_buf_unref(buf);
        (void)z_stow_client_slot_cancel(peer, slot, slot->seq);
        return ret;
    }
    net_buf_add(buf, len);
    return z_stow_client_slot_send(peer, slot, buf);
}

/**
 * @brief Describe the current group of names from @p start_id, or start the next group when none is being read
 */
static void send_describe(struct stow_client_peer* peer, uint32_t start_id)
{
    while (true)
    {
        if (peer->verify.count == 0U && peer->verify.start >= peer_item_count(peer))
        {
            verify_finished(peer);
            return;
        }

        peer->verify.describe_from = start_id;
        int ret = send_verify_request(peer, STOW_MSG_DESCRIBE, start_id);
        if (ret == -E2BIG && peer->verify.count == 0U)
        {
            LOG_ERR("Item %s cannot be named in a Describe request", peer_item(peer, peer->verify.start)->name);
            peer->items[peer->verify.start].availability = STOW_CLIENT_INCOMPATIBLE;
            peer->verify.start++;
            continue;
        }
        if (ret != SUCCESS)
        {
            LOG_ERR("Describe to session %u failed (%d)", peer->session.id, ret);
            z_stow_client_verify_failed(peer, ret);
        }
        return;
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
 * @brief Check a peer's item description against the expected item of the same name
 *
 * @details The expected constraints are encoded with the item's own codec and compared byte for byte.
 *
 * @return SUCCESS when the type and constraints match, -EINVAL when they differ, or -ENOMEM
 */
static int check_item(const struct stow_client_item* item, const struct stow_msg_description* description)
{
    if (description->type != (uint32_t)item->type)
    {
        return -EINVAL;
    }

    size_t len = description->constraints.len;
    void* expected = NULL;
    int ret = MEM_ALLOC(len, &expected);
    if (ret != SUCCESS)
    {
        return -ENOMEM;
    }

    zcbor_state_t encoder[STOW_MSG_WRITER_STATES];
    zcbor_new_encode_state(encoder, ARRAY_SIZE(encoder), expected, len, 1);
    ret = item->interface->encode_constraints(encoder, &item->constraints);
    if (ret == SUCCESS && ((size_t)(encoder[0].payload - (const uint8_t*)expected) != len ||
                           memcmp(expected, description->constraints.value, len) != 0))
    {
        ret = -EINVAL;
    }
    MEM_UNREF(&expected);
    return (ret == -ENOMEM) ? -EINVAL : ret;
}

/**
 * @brief Match each description to a bound item by name, marking the item available or incompatible
 *
 * @param peer Peer being verified
 * @param reader Reader of the Describe Response
 * @param count Number of descriptions in the response
 *
 * @return SUCCESS, or -ENOBUFS when no memory is free to check one
 */
static int check_descriptions(struct stow_client_peer* peer, struct stow_msg_reader* reader, uint16_t count)
{
    for (uint16_t i = 0; i < count; i++)
    {
        struct stow_msg_description description;
        (void)stow_msg_next_description(reader, &description);
        for (uint16_t id = 0; id < peer_item_count(peer); id++)
        {
            const struct stow_client_item* item = peer_item(peer, id);
            if (strlen(item->name) != description.name.len ||
                memcmp(item->name, description.name.value, description.name.len) != 0)
            {
                continue;
            }

            int ret = (description.id <= UINT16_MAX) ? check_item(item, &description) : -EINVAL;
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
    struct stow_client_peer* peer, enum stow_message_code request_code, int err, struct stow_msg_reader* reader,
    const struct stow_msg* msg)
{
    if (err == SUCCESS && msg->code == STOW_MSG_ERROR)
    {
        if (request_code == STOW_MSG_DESCRIBE && msg->error.err == -E2BIG)
        {
            describe_too_large(peer);
            return;
        }
        err = msg->error.err;
    }
    else if (err == SUCCESS && msg->code != stow_msg_answer(request_code))
    {
        err = -ENOMSG;
    }
    if (err != SUCCESS)
    {
        z_stow_client_verify_failed(peer, err);
        return;
    }

    if (request_code == STOW_MSG_HELLO)
    {
        peer->info.max_message = msg->hello.max_message;
        peer->info.roles = msg->hello.roles;
        send_describe(peer, 0U);
        return;
    }

    uint32_t next_id = msg->describe_response.next;
    int ret = check_descriptions(peer, reader, msg->count);
    if (ret == SUCCESS && next_id != STOW_MSG_ABSENT && next_id <= peer->verify.describe_from)
    {
        ret = -ENOMSG;
    }
    if (ret != SUCCESS)
    {
        z_stow_client_verify_failed(peer, ret);
        return;
    }
    if (next_id == STOW_MSG_ABSENT)
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

    LOG_WRN("Session %u could not be verified (%d)", peer->session.id, err);
    CALL_HOOKS(connected, peer, err);
}
