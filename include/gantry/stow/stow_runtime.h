/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Stow's internal session runtime, which the Stow server and client are instances of
 *
 * @details An instance owns a table of sessions, a thread and a queue, and is reached by transports through its
 * @ref stow_endpoint. Its role is a table of handlers the thread calls: a message arrived, a session opened or closed,
 * posted work is due, or a broadcast event must reach each session.
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#pragma once

#include <gantry/event.h>
#include <gantry/stow/stow_msg.h>
#include <gantry/stow/stow_transport.h>
#include <gantry/stow/types/stow_types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

/**
 * @addtogroup stow_protocol
 * @{
 */

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

struct stow_runtime;

/**
 * @brief A session, embedded in the role's per-session struct
 *
 * @details Roles read the first four fields and change only @ref stow_session.roles; the rest belong to the runtime.
 */
struct stow_session
{
    const struct stow_endpoint* endpoint; /**< Endpoint the session is on */
    uint32_t id;                          /**< ID the transport gave the session */
    stow_role_t roles;                    /**< Roles granted at open, which the role may change on its thread */
    struct stow_transport* transport;     /**< Transport of the session, valid from opened until closed */
    /** @cond INTERNAL_HIDDEN */
    struct stow_runtime* runtime;
    uint8_t state;
    uint8_t signals;
    uint32_t generation; /* Counts the sessions the slot has held, so a queued entry outliving its session is dropped */
    atomic_t refs;       /* One for the open session, and one per stow_session_ref */
    /** @endcond */
};

/**
 * @brief What an instance does with its sessions
 *
 * @details Handlers run on the instance's thread, except @ref stow_runtime_role.accept and
 * @ref stow_runtime_role.freed. A role sets exactly one of @ref stow_runtime_role.answer and
 * @ref stow_runtime_role.receive, and sets @ref stow_runtime_role.work or @ref stow_runtime_role.update when it posts
 * or broadcasts; any other handler may be NULL. Buffers and events handed to a handler are borrowed, and the runtime
 * releases them when it returns.
 */
struct stow_runtime_role
{
    /**
     * @brief Check a session that is opening, in the transport's context
     *
     * @details Must not block. Lookups do not see the session yet.
     *
     * @param session The session
     *
     * @return SUCCESS to open it, otherwise the error the open fails with
     */
    int (*accept)(struct stow_session* session);

    /**
     * @brief A session opened, before anything else for it arrives
     *
     * @param session The session
     */
    void (*opened)(struct stow_session* session);

    /**
     * @brief A session closed; called if and only if @ref stow_runtime_role.opened was
     *
     * @param session The session
     */
    void (*closed)(struct stow_session* session);

    /**
     * @brief Answer a message
     *
     * @details The runtime finishes and sends the answer, or an Error with the returned tag when the answer cannot be
     * finished.
     *
     * @param session Session the message arrived on
     * @param msg The message
     * @param reply Writer of the answer
     *
     * @return Tag of the answer
     */
    uint8_t (*answer)(struct stow_session* session, const struct net_buf* msg, struct stow_msg_writer* reply);

    /**
     * @brief Handle a message that gets no answer from the runtime
     *
     * @param session Session the message arrived on
     * @param msg The message
     */
    void (*receive)(struct stow_session* session, const struct net_buf* msg);

    /**
     * @brief Do work queued with @ref stow_runtime_post
     *
     * @param session The session
     * @param arg The posted argument
     * @param buf The posted buffer, or NULL
     */
    void (*work)(struct stow_session* session, uint32_t arg, struct net_buf* buf);

    /**
     * @brief Pass an event queued with @ref stow_runtime_broadcast to one opened session
     *
     * @param session The session
     * @param event The event, the same for every session
     */
    void (*update)(struct stow_session* session, event_t* event);

    /**
     * @brief Called after each pass over the signals and the queue
     *
     * @return Longest the thread waits before the next pass
     */
    k_timeout_t (*idle)(void);

    /**
     * @brief The last reference to a session was released, on any thread; must not block
     *
     * @details The session's slot is reused only after this returns.
     *
     * @param session The session
     */
    void (*freed)(struct stow_session* session);
};

/** @cond INTERNAL_HIDDEN */

enum stow_runtime_entry_kind
{
    STOW_RUNTIME_MESSAGE,
    STOW_RUNTIME_WORK,
    STOW_RUNTIME_BROADCAST,
};

/* An entry in an instance's queue, which owns its buffer or event */
struct stow_runtime_entry
{
    enum stow_runtime_entry_kind kind;
    struct stow_session* session;
    uint32_t generation;
    uint32_t arg;
    union
    {
        struct net_buf* buf; /* NULL for work posted without one */
        event_t* event;
    };
};

/** @endcond */

/**
 * @brief An instance of the runtime, defined with @ref STOW_RUNTIME_DEFINE
 */
struct stow_runtime
{
    /** @cond INTERNAL_HIDDEN */
    const struct stow_runtime_role* role;
    struct stow_session* sessions;
    size_t session_size;
    size_t session_count;
    k_thread_stack_t* stack;
    size_t stack_size;
    int priority;
    const char* name;
    struct k_spinlock lock; /* Guards membership, signals and generations */
    struct k_msgq queue;
    struct k_sem wake;
    struct k_thread thread;
    bool started;
    /** @endcond */
};

//**********************************************************
//* Definitions
//**********************************************************

/** @cond INTERNAL_HIDDEN */
int z_stow_runtime_open(
    struct stow_runtime* runtime, const struct stow_endpoint* endpoint, uint32_t session_id, stow_role_t roles,
    struct stow_transport* transport);
void z_stow_runtime_close(struct stow_runtime* runtime, const struct stow_endpoint* endpoint, uint32_t session_id);
void z_stow_runtime_receive(
    struct stow_runtime* runtime, const struct stow_endpoint* endpoint, uint32_t session_id, struct net_buf* buf);
/** @endcond */

/**
 * @brief Declare the endpoint functions of an instance, for a header that uses @ref STOW_RUNTIME_ENDPOINT
 *
 * @param _name Name of the instance
 */
#define STOW_RUNTIME_ENDPOINT_DECLARE(_name)                                                         \
    int z_stow_runtime_##_name##_opened(                                                             \
        const struct stow_endpoint* endpoint, uint32_t session_id, stow_role_t roles,                \
        struct stow_transport* transport);                                                           \
    void z_stow_runtime_##_name##_closed(const struct stow_endpoint* endpoint, uint32_t session_id); \
    void z_stow_runtime_##_name##_receive(                                                           \
        const struct stow_endpoint* endpoint, uint32_t session_id, struct net_buf* buf)

/**
 * @brief Initializer of a @ref stow_endpoint that opens sessions on an instance
 *
 * @details Opening a session fails with -EINVAL for a transport stow_transport_usable() rejects, -EALREADY when the
 * session is open, -ENOMEM when every session of the instance is open or still referenced, or the role's
 * @ref stow_runtime_role.accept error.
 *
 * @param _name Name of the instance
 */
#define STOW_RUNTIME_ENDPOINT(_name)                       \
    {                                                      \
        .session_opened = z_stow_runtime_##_name##_opened, \
        .session_closed = z_stow_runtime_##_name##_closed, \
        .receive = z_stow_runtime_##_name##_receive,       \
    }

/**
 * @brief Define an instance, with its own thread, queue and endpoint functions
 *
 * @param _name Name of the instance and its thread
 * @param _role The @ref stow_runtime_role
 * @param _sessions The role's array of per-session structs
 * @param _member Name of the @ref stow_session in each struct
 * @param _queue_size Number of entries the queue holds
 * @param _stack_size Stack size of the thread
 * @param _priority Priority of the thread
 */
#define STOW_RUNTIME_DEFINE(_name, _role, _sessions, _member, _queue_size, _stack_size, _priority)                   \
    static char __aligned(sizeof(void*)) _name##_queue_buffer[(_queue_size) * sizeof(struct stow_runtime_entry)];    \
    static K_THREAD_STACK_DEFINE(_name##_stack, _stack_size);                                                        \
    static struct stow_runtime _name = {                                                                             \
        .role = &(_role),                                                                                            \
        .sessions = &(_sessions)[0]._member,                                                                         \
        .session_size = sizeof((_sessions)[0]),                                                                      \
        .session_count = ARRAY_SIZE(_sessions),                                                                      \
        .queue =                                                                                                     \
            Z_MSGQ_INITIALIZER(_name.queue, _name##_queue_buffer, sizeof(struct stow_runtime_entry), (_queue_size)), \
        .wake = Z_SEM_INITIALIZER(_name.wake, 0, 1),                                                                 \
        .stack = _name##_stack,                                                                                      \
        .stack_size = K_THREAD_STACK_SIZEOF(_name##_stack),                                                          \
        .priority = (_priority),                                                                                     \
        .name = #_name,                                                                                              \
    };                                                                                                               \
    STOW_RUNTIME_ENDPOINT_DECLARE(_name);                                                                            \
    int z_stow_runtime_##_name##_opened(                                                                             \
        const struct stow_endpoint* endpoint, uint32_t session_id, stow_role_t roles,                                \
        struct stow_transport* transport)                                                                            \
    {                                                                                                                \
        return z_stow_runtime_open(&_name, endpoint, session_id, roles, transport);                                  \
    }                                                                                                                \
    void z_stow_runtime_##_name##_closed(const struct stow_endpoint* endpoint, uint32_t session_id)                  \
    {                                                                                                                \
        z_stow_runtime_close(&_name, endpoint, session_id);                                                          \
    }                                                                                                                \
    void z_stow_runtime_##_name##_receive(                                                                           \
        const struct stow_endpoint* endpoint, uint32_t session_id, struct net_buf* buf)                              \
    {                                                                                                                \
        z_stow_runtime_receive(&_name, endpoint, session_id, buf);                                                   \
    }

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Start an instance's thread
 *
 * @details Sessions that open before it starts are handled once it does.
 *
 * @param runtime The instance
 *
 * @return SUCCESS when the thread is started
 * @return -EALREADY when it already was
 */
int stow_runtime_start(struct stow_runtime* runtime);

/**
 * @brief Queue work for an open session, delivered to @ref stow_runtime_role.work on the instance's thread
 *
 * @details Work for a session that closes first is released without reaching the role.
 *
 * @param runtime The instance
 * @param endpoint Endpoint the session is on
 * @param session_id ID of the session
 * @param arg Passed to the role
 * @param buf Lent to the role, or NULL; ownership is always taken
 *
 * @return SUCCESS when the work is queued
 * @return -ENOTCONN when no session is open for @p endpoint and @p session_id
 * @return -ENOBUFS when the queue is full
 * @releases net_buf buf
 */
int stow_runtime_post(
    struct stow_runtime* runtime, const struct stow_endpoint* endpoint, uint32_t session_id, uint32_t arg,
    struct net_buf* buf);

/**
 * @brief Queue an event for @ref stow_runtime_role.update, called once for each opened session
 *
 * @param runtime The instance
 * @param event The event; ownership is always taken
 *
 * @return SUCCESS when the event is queued
 * @return -ENOBUFS when the queue is full
 * @releases memory event
 */
int stow_runtime_broadcast(struct stow_runtime* runtime, event_t* event);

/**
 * @brief Check whether the caller runs on an instance's thread
 *
 * @param runtime The instance
 *
 * @return true on the instance's thread
 */
static inline bool stow_runtime_in_thread(const struct stow_runtime* runtime)
{
    return k_current_get() == &runtime->thread;
}

/**
 * @brief Take a reference to a session, keeping its slot from reuse after it closes
 *
 * @details Take it while the session is borrowed or already referenced.
 *
 * @param session The session
 *
 * @return @p session
 * @acquires stow_session return
 */
struct stow_session* stow_session_ref(struct stow_session* session);

/**
 * @brief Release a reference to a session and clear the pointer
 *
 * @param[in,out] session The reference to release, or a pointer to NULL
 * @releases stow_session session
 */
void stow_session_unref(struct stow_session** session);

/**
 * @}
 */
