/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Stow client internals shared by its request, slot and verification code
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#pragma once

#include <gantry/stow/stow_client.h>
#include <gantry/stow/stow_transport.h>
#include <gantry/stow/stow_wire.h>
#include <stdbool.h>
#include <stdint.h>
#include <zcbor_common.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/iterable_sections.h>

//**********************************************************
//* Definitions
//**********************************************************

/**
 * @brief Number of zcbor encoder/decoder state slots
 */
#define ZCBOR_STATES 4

/**
 * @brief Number of request tags
 */
#define TAG_COUNT (STOW_TAG_MAX + 1U)

/**
 * @brief Call one hook of every registered @ref stow_client_cb that sets it
 */
#define CALL_HOOKS(_hook, ...)                     \
    STRUCT_SECTION_FOREACH(stow_client_cb, _hooks) \
    {                                              \
        if (_hooks->_hook != NULL)                 \
        {                                          \
            _hooks->_hook(__VA_ARGS__);            \
        }                                          \
    }

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief Connection state of a peer
 */
enum peer_state
{
    PEER_CLOSED,    /**< No session is open */
    PEER_VERIFYING, /**< The session is open and the peer is being checked */
    PEER_READY,     /**< The peer is connected */
    PEER_FAILED,    /**< The session is open but the peer could not be verified */
};

/**
 * @brief Signals the transport raises on a peer
 */
enum peer_signal
{
    PEER_SIGNAL_OPEN,  /**< The session opened */
    PEER_SIGNAL_CLOSE, /**< The session closed */
    PEER_SIGNAL_COUNT, /**< Number of signals */
};

/**
 * @brief State of a pending request slot
 */
enum slot_state
{
    SLOT_FREE,   /**< Unused */
    SLOT_QUEUED, /**< Waiting for the client thread to send it */
    SLOT_SENT,   /**< Taken by the transport or being failed */
};

/**
 * @brief A caller blocked on a request
 */
struct sync_call
{
    struct k_sem done;                /**< Given when the request finishes */
    int err;                          /**< Outcome of the request */
    struct stow_client_value* values; /**< Where a Get's values go */
};

/**
 * @brief A pending request
 */
struct slot
{
    enum slot_state state;       /**< Slot state */
    uint8_t tag;                 /**< Tag of the request */
    bool internal;               /**< Whether the client sent it to verify the peer */
    enum stow_message_code code; /**< Message code of the request */
    uint16_t count;              /**< Number of entries */
    uint16_t* ids;               /**< Copy of a Get's item IDs, to check its response against */
    uint32_t seq;                /**< Changes each time the slot is taken, so stale sends are dropped */
    int64_t deadline;            /**< Uptime in ms when a sent request times out */
    stow_client_cb_t cb;         /**< Callback of an asynchronous request */
    void* user_data;             /**< Passed to @ref slot.cb */
    event_t* event;              /**< Result event of an asynchronous request, allocated when it is queued */
    struct sync_call* sync;      /**< Caller blocked on the request */
};

/**
 * @brief What the client knows about one expected item on a peer
 */
struct item_state
{
    uint16_t remote_id;                         /**< The peer's ID for the item */
    enum stow_client_availability availability; /**< Whether the peer has the item */
};

/**
 * @brief A peer's pending requests and tags, owned by the slot code
 */
struct peer_slots
{
    struct slot entries[CONFIG_STOW_CLIENT_MAX_PENDING]; /**< Pending requests */
    uint32_t tags_busy;                                  /**< Tags of pending requests */
    uint32_t quarantined;                                /**< Tags that may still get a late response */
    int64_t quarantine_until[TAG_COUNT];                 /**< Uptime in ms when each quarantined tag frees */
    uint32_t seq;                                        /**< Last slot sequence number handed out */
};

/**
 * @brief Where a peer's verification is, owned by the verification code
 */
struct peer_verify
{
    uint16_t start;         /**< First item of the Describe group being read */
    uint16_t count;         /**< Number of items in the Describe group */
    uint16_t singly_until;  /**< Items before this are described one at a time */
    uint32_t describe_from; /**< ID the last Describe started from */
};

/**
 * @brief A peer instance
 */
struct stow_client_peer
{
    bool in_use;                                 /**< Slot holds a peer */
    atomic_t refs;                               /**< References from the open session, the app and events */
    const struct stow_client_endpoint* endpoint; /**< Endpoint the session is on */
    uint32_t session_id;                         /**< ID the transport gave the session */
    uint32_t timeout_ms;                         /**< Request timeout */
    struct item_state* items;                    /**< One entry per expected item */
    struct stow_transport* transport;            /**< Transport of the open session, or NULL */
    enum peer_state state;                       /**< Connection state */
    ATOMIC_DEFINE(signals, PEER_SIGNAL_COUNT);   /**< Pending @ref peer_signal bits */
    struct stow_client_peer_info info;           /**< What the peer reported in its Hello Response */
    struct k_spinlock lock;                      /**< Guards the slots, tags and connection state */
    struct k_sem slot_free;                      /**< Given when a slot or tag may have freed */
    struct peer_slots slots;                     /**< Pending requests and tags */
    struct peer_verify verify;                   /**< Verification progress */
    uint32_t opens;                              /**< Counts the sessions the slot has held */
};

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Get the schema entry of an expected item
 *
 * @param peer The peer
 * @param id Generated item ID, already checked against the schema
 *
 * @return The item
 */
static inline const struct stow_client_item* peer_item(const struct stow_client_peer* peer, uint16_t id)
{
    return &peer->endpoint->schema->items[id];
}

/**
 * @brief Get the number of items a peer is expected to have
 *
 * @param peer The peer
 *
 * @return The number of generated item IDs
 */
static inline uint16_t peer_item_count(const struct stow_client_peer* peer)
{
    return peer->endpoint->schema->item_count;
}

/**
 * @brief Take a free slot and tag, filled from @p pending
 *
 * @note Call with the peer locked.
 *
 * @param peer The peer
 * @param pending What the slot holds
 * @param[out] retry_at When no tag is free, uptime in ms when one frees, otherwise INT64_MAX
 *
 * @return The queued slot, or NULL when no slot or tag is free
 */
struct slot* z_stow_client_slot_take(struct stow_client_peer* peer, const struct slot* pending, int64_t* retry_at);

/**
 * @brief Check whether a peer has a free slot
 *
 * @note Call with the peer locked.
 *
 * @param peer The peer
 *
 * @return true when a slot is free
 */
bool z_stow_client_slot_any_free(const struct stow_client_peer* peer);

/**
 * @brief Free a slot that was never sent, in one lock section so the client thread never sees it half freed
 *
 * @param peer The peer
 * @param slot The slot
 * @param seq Sequence number the slot had when it was taken
 *
 * @return true when the slot was freed, false when the client thread already finished the request
 */
bool z_stow_client_slot_cancel(struct stow_client_peer* peer, struct slot* slot, uint32_t seq);

/**
 * @brief Free a request's slot, then report its outcome to the blocked caller or through its result event
 *
 * @param peer The peer
 * @param slot The request's slot
 * @param err SUCCESS or a negative errno
 * @param index Entry that failed, or @ref STOW_CLIENT_NO_INDEX
 */
void z_stow_client_slot_finish(struct stow_client_peer* peer, struct slot* slot, int err, uint16_t index);

/**
 * @brief Finish a request with an error
 *
 * @param peer The peer
 * @param slot The request's slot
 * @param err Negative errno
 */
void z_stow_client_slot_fail(struct stow_client_peer* peer, struct slot* slot, int err);

/**
 * @brief Fail every pending request with -ENOTCONN
 *
 * @details Claims each slot under the lock first, so a caller cancelling the same slot cannot finish it too.
 *
 * @param peer The peer
 */
void z_stow_client_slot_fail_all(struct stow_client_peer* peer);

/**
 * @brief Hand a queued request to the transport and start its timeout, or fail it with -EBUSY when refused
 *
 * @param peer The peer
 * @param slot The request's slot
 * @param buf The request, ownership is always taken
 *
 * @return SUCCESS, or -EBUSY when the transport refused the request
 * @releases net_buf buf
 */
int z_stow_client_slot_send(struct stow_client_peer* peer, struct slot* slot, struct net_buf* buf);

/**
 * @brief Find the sent request a response answers
 *
 * @param peer The peer
 * @param tag Tag of the response
 *
 * @return The request's slot, or NULL
 */
struct slot* z_stow_client_slot_find_sent(struct stow_client_peer* peer, uint8_t tag);

/**
 * @brief Find when a peer's next sent request times out
 *
 * @param peer The peer
 *
 * @return Uptime in ms, or INT64_MAX when nothing is sent
 */
int64_t z_stow_client_slots_deadline(const struct stow_client_peer* peer);

/**
 * @brief Fail every sent request whose time is up, and quarantine its tag for a late response
 *
 * @param peer The peer
 * @param now Uptime in ms
 *
 * @return true when a verification request timed out
 */
bool z_stow_client_slots_expire(struct stow_client_peer* peer, int64_t now);

/**
 * @brief Release a tag answered by a response that matches no sent request
 *
 * @param peer The peer
 * @param tag Tag of the response
 *
 * @return true when the tag was quarantined, so the response came late
 */
bool z_stow_client_tag_answered(struct stow_client_peer* peer, uint8_t tag);

/**
 * @brief Start verifying a peer whose session opened by sending Hello
 *
 * @param peer The peer
 */
void z_stow_client_verify_start(struct stow_client_peer* peer);

/**
 * @brief Continue verification with the response to a Hello or Describe
 *
 * @param peer The peer
 * @param request_code Message code of the request answered
 * @param code Message code of the response
 * @param decoder Decoder positioned after the response's tag
 */
void z_stow_client_verify_response(
    struct stow_client_peer* peer, enum stow_message_code request_code, uint32_t code, zcbor_state_t* decoder);

/**
 * @brief Stop verifying a peer and report why through the connected hooks
 *
 * @param peer The peer
 * @param err Negative errno
 */
void z_stow_client_verify_failed(struct stow_client_peer* peer, int err);
