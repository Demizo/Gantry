/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Stow session runtime
 *
 * @details Transports open, close and deliver to sessions from their own context; the instance's thread handles the
 * signals and queued entries that leaves, and calls the role.
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#include <gantry/error.h>
#include <gantry/event.h>
#include <gantry/ownership.h>
#include <gantry/stow/stow_msg.h>
#include <gantry/stow/stow_runtime.h>
#include <gantry/stow/stow_transport.h>
#include <stdbool.h>
#include <sys/errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(stow_runtime, CONFIG_STOW_RUNTIME_LOG_LEVEL);

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief Where a session slot is in its life
 */
enum session_state
{
    SESSION_FREE,    /**< The slot can be reserved */
    SESSION_OPENING, /**< Reserved while the role accepts it, unseen by lookups */
    SESSION_OPEN,    /**< Open until its close is handled */
    SESSION_CLOSED,  /**< Closed, and still referenced */
};

/**
 * @brief Signals raised from the transport's context, as bits so a full queue cannot lose them
 */
enum session_signal
{
    SIGNAL_OPEN = BIT(0),  /**< The session opened */
    SIGNAL_CLOSE = BIT(1), /**< The session closed */
};

//**********************************************************
//* Static Function Declarations
//**********************************************************

static struct stow_session* session_at(const struct stow_runtime* runtime, size_t index);
static bool is_findable(const struct stow_session* session);
static bool is_deliverable(const struct stow_session* session);
static struct stow_session* find_session(
    const struct stow_runtime* runtime, const struct stow_endpoint* endpoint, uint32_t session_id);
static int reserve(
    struct stow_runtime* runtime, const struct stow_endpoint* endpoint, uint32_t session_id,
    struct stow_session** session_out);
static bool bind_to_session(
    struct stow_runtime* runtime, const struct stow_endpoint* endpoint, uint32_t session_id,
    struct stow_runtime_entry* entry);
static int enqueue(struct stow_runtime* runtime, const struct stow_runtime_entry* entry);

static void close_if_signalled(struct stow_runtime* runtime, struct stow_session* session);
static void open_if_signalled(struct stow_runtime* runtime, struct stow_session* session);
static void handle_signals(struct stow_runtime* runtime);
static struct net_buf* take_buf(struct stow_runtime_entry* entry);
static void release_buf(struct net_buf* buf);
static event_t* take_event(struct stow_runtime_entry* entry);
static void reply(struct stow_runtime* runtime, struct stow_session* session, const struct net_buf* msg);
static void deliver(struct stow_runtime* runtime, const struct stow_runtime_entry* entry, struct net_buf* buf);
static void deliver_update(struct stow_runtime* runtime, event_t* event);
static bool handle_next_entry(struct stow_runtime* runtime);
static void drain(struct stow_runtime* runtime);
static void run(void* p1, void* p2, void* p3);

//**********************************************************
//* Static Function Definitions
//**********************************************************

static struct stow_session* session_at(const struct stow_runtime* runtime, size_t index)
{
    return (struct stow_session*)((uint8_t*)runtime->sessions + index * runtime->session_size);
}

/**
 * @brief Check that lookups by endpoint and ID find a session
 *
 * @note Call with the instance locked.
 */
static bool is_findable(const struct stow_session* session)
{
    return session->state == SESSION_OPEN && (session->signals & SIGNAL_CLOSE) == 0U;
}

/**
 * @brief Check that queued entries for a session may reach the role
 *
 * @note Call with the instance locked.
 */
static bool is_deliverable(const struct stow_session* session)
{
    return session->state == SESSION_OPEN && session->signals == 0U;
}

/**
 * @note Call with the instance locked.
 */
static struct stow_session* find_session(
    const struct stow_runtime* runtime, const struct stow_endpoint* endpoint, uint32_t session_id)
{
    for (size_t i = 0; i < runtime->session_count; i++)
    {
        struct stow_session* session = session_at(runtime, i);
        if (is_findable(session) && session->endpoint == endpoint && session->id == session_id)
        {
            return session;
        }
    }
    return NULL;
}

/**
 * @brief Reserve a free slot for a session, unless the session is open or opening
 *
 * @return SUCCESS, -EALREADY or -ENOMEM
 */
static int reserve(
    struct stow_runtime* runtime, const struct stow_endpoint* endpoint, uint32_t session_id,
    struct stow_session** session_out)
{
    struct stow_session* free_session = NULL;
    int ret = SUCCESS;
    k_spinlock_key_t key = k_spin_lock(&runtime->lock);
    for (size_t i = 0; ret == SUCCESS && i < runtime->session_count; i++)
    {
        struct stow_session* session = session_at(runtime, i);
        if (session->state == SESSION_FREE)
        {
            free_session = (free_session == NULL) ? session : free_session;
        }
        else if (
            (session->state == SESSION_OPENING || is_findable(session)) && session->endpoint == endpoint &&
            session->id == session_id)
        {
            ret = -EALREADY;
        }
    }
    if (ret == SUCCESS && free_session == NULL)
    {
        ret = -ENOMEM;
    }
    if (ret == SUCCESS)
    {
        free_session->state = SESSION_OPENING;
        free_session->runtime = runtime;
        free_session->endpoint = endpoint;
        free_session->id = session_id;
        free_session->signals = 0U;
        free_session->generation++;
        *session_out = free_session;
    }
    k_spin_unlock(&runtime->lock, key);
    return ret;
}

/**
 * @brief Address an entry to an open session and its current generation
 *
 * @return false when no such session is open
 */
static bool bind_to_session(
    struct stow_runtime* runtime, const struct stow_endpoint* endpoint, uint32_t session_id,
    struct stow_runtime_entry* entry)
{
    k_spinlock_key_t key = k_spin_lock(&runtime->lock);
    struct stow_session* session = find_session(runtime, endpoint, session_id);
    if (session != NULL)
    {
        entry->session = session;
        entry->generation = session->generation;
    }
    k_spin_unlock(&runtime->lock, key);
    return session != NULL;
}

/**
 * @brief Queue an entry, which then owns its buffer or event, and wake the thread
 *
 * @return SUCCESS, or -ENOBUFS when the queue is full
 */
static int enqueue(struct stow_runtime* runtime, const struct stow_runtime_entry* entry)
{
    if (k_msgq_put(&runtime->queue, entry, K_NO_WAIT) != 0)
    {
        return -ENOBUFS;
    }
    k_sem_give(&runtime->wake);
    return SUCCESS;
}

/**
 * @brief Handle a session's close, dropping the reference the open session held
 */
static void close_if_signalled(struct stow_runtime* runtime, struct stow_session* session)
{
    k_spinlock_key_t key = k_spin_lock(&runtime->lock);
    uint8_t signals = session->signals;
    if ((signals & SIGNAL_CLOSE) != 0U)
    {
        session->signals = 0U;
        session->state = SESSION_CLOSED;
    }
    k_spin_unlock(&runtime->lock, key);

    if ((signals & SIGNAL_CLOSE) == 0U)
    {
        return;
    }
    if ((signals & SIGNAL_OPEN) == 0U && runtime->role->closed != NULL)
    {
        runtime->role->closed(session);
    }
    session->transport = NULL;
    stow_session_unref(&session);
}

/**
 * @brief Handle a session's open, unless a close is already pending
 */
static void open_if_signalled(struct stow_runtime* runtime, struct stow_session* session)
{
    k_spinlock_key_t key = k_spin_lock(&runtime->lock);
    bool opening = (session->signals == SIGNAL_OPEN);
    if (opening)
    {
        session->signals = 0U;
    }
    k_spin_unlock(&runtime->lock, key);

    if (opening && runtime->role->opened != NULL)
    {
        runtime->role->opened(session);
    }
}

/**
 * @brief Handle every session's close, then every session's open, so a session reopened in another slot opens after
 * its earlier session closes
 */
static void handle_signals(struct stow_runtime* runtime)
{
    for (size_t i = 0; i < runtime->session_count; i++)
    {
        close_if_signalled(runtime, session_at(runtime, i));
    }
    for (size_t i = 0; i < runtime->session_count; i++)
    {
        open_if_signalled(runtime, session_at(runtime, i));
    }
}

/** @acquires net_buf return */
static struct net_buf* take_buf(struct stow_runtime_entry* entry)
{
    struct net_buf* buf = entry->buf;
    entry->buf = NULL;
    return buf;
}

/** @releases net_buf buf */
static void release_buf(struct net_buf* buf)
{
    if (buf != NULL)
    {
        net_buf_unref(buf);
    }
}

/** @acquires memory return */
static event_t* take_event(struct stow_runtime_entry* entry)
{
    event_t* event = entry->event;
    entry->event = NULL;
    return event;
}

static void reply(struct stow_runtime* runtime, struct stow_session* session, const struct net_buf* msg)
{
    struct net_buf* buf = NULL;
    size_t budget = 0;
    int ret = stow_transport_alloc(session->transport, 0U, &buf, &budget);
    if (ret != SUCCESS)
    {
        LOG_ERR("No response buffer for session %u (%d)", session->id, ret);
        return;
    }
    struct stow_msg_writer writer;
    stow_msg_writer_init(&writer, net_buf_tail(buf), budget);
    uint8_t tag = runtime->role->answer(session, msg, &writer);

    size_t len = 0;
    ret = stow_msg_finish(&writer, &len);
    if (ret != SUCCESS)
    {
        const struct stow_msg error = {
            .code = STOW_MSG_ERROR,
            .tag = tag,
            .error = { ret, STOW_MSG_ABSENT, STOW_MSG_ABSENT },
        };
        (void)stow_msg_write(&writer, &error);
        ret = stow_msg_finish(&writer, &len);
    }
    if (ret != SUCCESS)
    {
        LOG_ERR("Transport buffers cannot hold an Error, dropping the response");
        net_buf_unref(buf);
        return;
    }

    net_buf_add(buf, len);
    ret = stow_transport_send(session->transport, session->id, buf);
    if (ret != SUCCESS)
    {
        LOG_WRN("Dropped a message to session %u (%d)", session->id, ret);
    }
}

/**
 * @brief Hand a message or work to the role, unless its session closed since it was queued
 */
static void deliver(struct stow_runtime* runtime, const struct stow_runtime_entry* entry, struct net_buf* buf)
{
    struct stow_session* session = entry->session;
    k_spinlock_key_t key = k_spin_lock(&runtime->lock);
    bool current = is_deliverable(session) && session->generation == entry->generation;
    k_spin_unlock(&runtime->lock, key);
    if (!current)
    {
        LOG_WRN("Dropped an entry queued for session %u, which closed", session->id);
        return;
    }

    const struct stow_runtime_role* role = runtime->role;
    if (entry->kind == STOW_RUNTIME_WORK)
    {
        role->work(session, entry->arg, buf);
    }
    else if (role->answer != NULL)
    {
        reply(runtime, session, buf);
    }
    else
    {
        role->receive(session, buf);
    }
}

static void deliver_update(struct stow_runtime* runtime, event_t* event)
{
    for (size_t i = 0; i < runtime->session_count; i++)
    {
        struct stow_session* session = session_at(runtime, i);
        k_spinlock_key_t key = k_spin_lock(&runtime->lock);
        bool deliverable = is_deliverable(session);
        k_spin_unlock(&runtime->lock, key);
        if (deliverable)
        {
            runtime->role->update(session, event);
        }
    }
}

static bool handle_next_entry(struct stow_runtime* runtime)
{
    struct stow_runtime_entry entry;
    if (k_msgq_get(&runtime->queue, &entry, K_NO_WAIT) != 0)
    {
        return false;
    }

    /* The entry's session opened before the entry was queued, so its open is handled first */
    handle_signals(runtime);

    if (entry.kind == STOW_RUNTIME_BROADCAST)
    {
        event_t* event = take_event(&entry);
        deliver_update(runtime, event);
        EVENT_UNREF(&event);
        return true;
    }
    struct net_buf* buf = take_buf(&entry);
    deliver(runtime, &entry, buf);
    release_buf(buf);
    return true;
}

static void drain(struct stow_runtime* runtime)
{
    handle_signals(runtime);
    while (handle_next_entry(runtime))
    {
    }
}

static void run(void* p1, void* p2, void* p3)
{
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    struct stow_runtime* runtime = p1;

    k_timeout_t wait = K_FOREVER;
    while (true)
    {
        (void)k_sem_take(&runtime->wake, wait);
        drain(runtime);
        wait = (runtime->role->idle != NULL) ? runtime->role->idle() : K_FOREVER;
    }
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

/** @cond INTERNAL_HIDDEN */
int z_stow_runtime_open(
    struct stow_runtime* runtime, const struct stow_endpoint* endpoint, uint32_t session_id, stow_role_t roles,
    struct stow_transport* transport)
{
    if (!stow_transport_usable(transport))
    {
        return -EINVAL;
    }

    struct stow_session* session = NULL;
    int ret = reserve(runtime, endpoint, session_id, &session);
    if (ret != SUCCESS)
    {
        return ret;
    }
    session->roles = roles;
    session->transport = transport;
    if (runtime->role->accept != NULL)
    {
        ret = runtime->role->accept(session);
    }

    k_spinlock_key_t key = k_spin_lock(&runtime->lock);
    if (ret == SUCCESS)
    {
        atomic_set(&session->refs, 1);
        session->signals = SIGNAL_OPEN;
        session->state = SESSION_OPEN;
    }
    else
    {
        session->state = SESSION_FREE;
    }
    k_spin_unlock(&runtime->lock, key);

    if (ret == SUCCESS)
    {
        k_sem_give(&runtime->wake);
    }
    return ret;
}

void z_stow_runtime_close(struct stow_runtime* runtime, const struct stow_endpoint* endpoint, uint32_t session_id)
{
    k_spinlock_key_t key = k_spin_lock(&runtime->lock);
    struct stow_session* session = find_session(runtime, endpoint, session_id);
    if (session != NULL)
    {
        session->signals |= SIGNAL_CLOSE;
    }
    k_spin_unlock(&runtime->lock, key);

    if (session == NULL)
    {
        LOG_WRN("Close for session %u that is not open", session_id);
        return;
    }
    k_sem_give(&runtime->wake);
}

void z_stow_runtime_receive(
    struct stow_runtime* runtime, const struct stow_endpoint* endpoint, uint32_t session_id, struct net_buf* buf)
{
    struct stow_runtime_entry entry = { .kind = STOW_RUNTIME_MESSAGE };
    if (buf == NULL || !bind_to_session(runtime, endpoint, session_id, &entry))
    {
        LOG_WRN("Dropping a message for session %u that is not open", session_id);
        return;
    }

    struct net_buf* held = net_buf_ref(buf);
    entry.buf = held;
    if (enqueue(runtime, &entry) != SUCCESS)
    {
        LOG_WRN("Dropping a message for session %u, queue full", session_id);
        net_buf_unref(held);
        return;
    }
    PASS_OWNERSHIP(held);
}
/** @endcond */

int stow_runtime_start(struct stow_runtime* runtime)
{
    const struct stow_runtime_role* role = runtime->role;
    ASSERT((role->answer == NULL) != (role->receive == NULL), "A role sets exactly one of answer and receive");

    k_spinlock_key_t key = k_spin_lock(&runtime->lock);
    bool started = runtime->started;
    runtime->started = true;
    k_spin_unlock(&runtime->lock, key);
    if (started)
    {
        return -EALREADY;
    }

    k_tid_t thread = k_thread_create(
        &runtime->thread, runtime->stack, runtime->stack_size, run, runtime, NULL, NULL, runtime->priority, 0,
        K_NO_WAIT);
    k_thread_name_set(thread, runtime->name);
    return SUCCESS;
}

int stow_runtime_post(
    struct stow_runtime* runtime, const struct stow_endpoint* endpoint, uint32_t session_id, uint32_t arg,
    struct net_buf* buf)
{
    struct stow_runtime_entry entry = { .kind = STOW_RUNTIME_WORK, .arg = arg, .buf = buf };
    int ret = bind_to_session(runtime, endpoint, session_id, &entry) ? enqueue(runtime, &entry) : -ENOTCONN;
    if (ret != SUCCESS)
    {
        release_buf(buf);
        return ret;
    }
    PASS_OWNERSHIP(buf);
    return SUCCESS;
}

int stow_runtime_broadcast(struct stow_runtime* runtime, event_t* event)
{
    const struct stow_runtime_entry entry = { .kind = STOW_RUNTIME_BROADCAST, .event = event };
    if (enqueue(runtime, &entry) != SUCCESS)
    {
        EVENT_UNREF(&event);
        return -ENOBUFS;
    }
    PASS_OWNERSHIP(event);
    return SUCCESS;
}

struct stow_session* stow_session_ref(struct stow_session* session)
{
    atomic_val_t previous = atomic_inc(&session->refs);
    ASSERT(previous > 0, "Session referenced after its last reference was released");
    ARG_UNUSED(previous);
    return session;
}

void stow_session_unref(struct stow_session** session)
{
    struct stow_session* released = *session;
    *session = NULL;
    if (released == NULL || atomic_dec(&released->refs) != 1)
    {
        return;
    }

    struct stow_runtime* runtime = released->runtime;
    if (runtime->role->freed != NULL)
    {
        runtime->role->freed(released);
    }
    k_spinlock_key_t key = k_spin_lock(&runtime->lock);
    released->state = SESSION_FREE;
    k_spin_unlock(&runtime->lock, key);
}
