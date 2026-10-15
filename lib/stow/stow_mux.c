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
#include <gantry/stow/stow_mux.h>
#include <gantry/stow/stow_wire.h>
#include <sys/errno.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/atomic.h>

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(stow_mux, CONFIG_STOW_CLIENT_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

/**
 * @brief Number of zcbor encoder/decoder state slots
 */
#define ZCBOR_STATES 4

//**********************************************************
//* Static Function Declarations
//**********************************************************

static void reply_error(struct stow_mux* mux, enum stow_error_code code, uint8_t tag);

//**********************************************************
//* Static Function Definitions
//**********************************************************

/**
 * @brief Answer a request that has no server
 *
 * @details The answer is an Error, which is never itself answered, so two muxes cannot keep answering each other.
 */
static void reply_error(struct stow_mux* mux, enum stow_error_code code, uint8_t tag)
{
    struct stow_transport* transport = mux->transport;
    struct net_buf* buf = NULL;
    zcbor_state_t encoder[ZCBOR_STATES];
    if (stow_wire_message_alloc(transport, 0U, &buf, encoder, ARRAY_SIZE(encoder)) != SUCCESS)
    {
        LOG_WRN("Dropped a request for session %u that has no server", mux->session_id);
        return;
    }

    const struct stow_serde_error error = {
        .code = code,
        .tag = tag,
        .item = STOW_SERDE_ABSENT,
        .index = STOW_SERDE_ABSENT,
    };
    int ret = stow_wire_encode_error(encoder, &error);
    if (ret == SUCCESS)
    {
        stow_wire_message_end(buf, encoder);
        ret = transport->send(transport, mux->session_id, buf);
    }
    if (ret != SUCCESS)
    {
        LOG_WRN("Dropped the answer to a request for session %u that has no server (%d)", mux->session_id, ret);
        net_buf_unref(buf);
        return;
    }
    PASS_OWNERSHIP(buf);
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
    zcbor_state_t decoder[ZCBOR_STATES];
    zcbor_new_decode_state(decoder, ARRAY_SIZE(decoder), buf->data, buf->len, 1, NULL, 0);

    uint32_t code = STOW_SERDE_ABSENT;
    uint8_t tag = STOW_TAG_NONE;
    (void)stow_wire_decode_start(decoder, &code, &tag);
    if (code >= STOW_MSG_COUNT || stow_wire_is_request(code))
    {
        if (mux->server != NULL)
        {
            mux->server->receive(mux->server, mux->session_id, buf);
        }
        else
        {
            reply_error(mux, (code == STOW_SERDE_ABSENT) ? STOW_ERR_MALFORMED_MSG : STOW_ERR_UNKNOWN_MSG, tag);
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
