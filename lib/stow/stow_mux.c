/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Routing for a carrier session that can carry a client session
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#include <gantry/error.h>
#include <gantry/memory.h>
#include <gantry/stow/stow_msg.h>
#include <gantry/stow/stow_mux.h>
#include <sys/errno.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/atomic.h>

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(stow_mux, CONFIG_STOW_CLIENT_LOG_LEVEL);

//**********************************************************
//* Static Function Declarations
//**********************************************************

static void reply_error(struct stow_mux* mux, int err, uint8_t tag);

//**********************************************************
//* Static Function Definitions
//**********************************************************

/**
 * @brief Answer a request that has no server
 *
 * @details The answer is an Error, which is never itself answered, so two muxes cannot keep answering each other.
 */
static void reply_error(struct stow_mux* mux, int err, uint8_t tag)
{
    struct stow_transport* transport = mux->transport;
    struct net_buf* buf = NULL;
    size_t budget = 0;
    if (stow_transport_alloc(transport, 0U, &buf, &budget) != SUCCESS)
    {
        LOG_WRN("Dropped a request for session %u that has no server", mux->session_id);
        return;
    }

    struct stow_msg_writer writer;
    stow_msg_writer_init(&writer, net_buf_tail(buf), budget);
    const struct stow_msg error = {
        .code = STOW_MSG_ERROR,
        .tag = tag,
        .error = { err, STOW_MSG_ABSENT, STOW_MSG_ABSENT },
    };
    (void)stow_msg_write(&writer, &error);
    size_t len = 0;
    int ret = stow_msg_finish(&writer, &len);
    if (ret == SUCCESS)
    {
        net_buf_add(buf, len);
        ret = stow_transport_send(transport, mux->session_id, buf);
    }
    else
    {
        net_buf_unref(buf);
    }
    if (ret != SUCCESS)
    {
        LOG_WRN("Dropped the answer to a request for session %u that has no server (%d)", mux->session_id, ret);
    }
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

int stow_mux_open_client(struct stow_mux* mux, const struct stow_endpoint* client)
{
    if (client == NULL)
    {
        return -EINVAL;
    }

    struct k_mutex* lock = &mux->client_lock;
    k_mutex_lock(lock, K_FOREVER);
    int ret = !atomic_get(&mux->open) ? -ENOTCONN : (atomic_ptr_get(&mux->client) != NULL) ? -EALREADY : SUCCESS;
    if (ret == SUCCESS)
    {
        atomic_ptr_set(&mux->client, (atomic_ptr_val_t)client);
        ret = client->session_opened(client, mux->session_id, 0, mux->transport);
        if (ret != SUCCESS)
        {
            atomic_ptr_clear(&mux->client);
        }
    }
    k_mutex_unlock(lock);
    return ret;
}

void stow_mux_close_client(struct stow_mux* mux)
{
    struct k_mutex* lock = &mux->client_lock;
    k_mutex_lock(lock, K_FOREVER);
    const struct stow_endpoint* client = atomic_ptr_clear(&mux->client);
    if (client != NULL)
    {
        client->session_closed(client, mux->session_id);
    }
    k_mutex_unlock(lock);
}

void stow_mux_receive(struct stow_mux* mux, struct net_buf* buf)
{
    uint32_t code = STOW_MSG_ABSENT;
    uint8_t tag = STOW_TAG_NONE;
    (void)stow_msg_peek(buf->data, buf->len, &code, &tag);
    if (code >= STOW_MSG_COUNT || stow_msg_is_request(code))
    {
        if (mux->server != NULL)
        {
            mux->server->receive(mux->server, mux->session_id, buf);
        }
        else if (atomic_get(&mux->open))
        {
            /* Only an open mux has a transport stow_transport_usable() accepted */
            reply_error(mux, (code == STOW_MSG_ABSENT) ? -EBADMSG : -EPROTO, tag);
        }
        return;
    }

    const struct stow_endpoint* client = atomic_ptr_get(&mux->client);
    if (client == NULL)
    {
        LOG_WRN("Dropped message %u for session %u, which has no client session open", code, mux->session_id);
        return;
    }
    client->receive(client, mux->session_id, buf);
}
