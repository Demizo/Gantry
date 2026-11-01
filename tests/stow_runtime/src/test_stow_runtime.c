/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gantry/error.h>
#include <gantry/event.h>
#include <gantry/memory.h>
#include <gantry/stow/stow_msg.h>
#include <gantry/stow/stow_runtime.h>
#include <gantry/stow/stow_transport.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

#include "test_settle.h"

#define TEST_PRIORITY 5
#define TEST_QUEUE_SIZE 4
#define TEST_BUF_COUNT 8
#define TEST_BUF_SIZE 64
#define TEST_EVENT_ID 0x7E57

/** Work with this argument blocks the thread until @ref open_gate */
#define GATE UINT32_MAX

/** A message whose first byte is this gets an answer too large for any buffer */
#define TOO_LARGE 0xFFU

#define SESSION_A 1
#define SESSION_B 2
#define SESSION_C 3

enum call_kind
{
    CALL_ACCEPT,
    CALL_OPENED,
    CALL_CLOSED,
    CALL_ANSWER,
    CALL_RECEIVE,
    CALL_WORK,
    CALL_UPDATE,
    CALL_FREED,
};

struct call
{
    enum call_kind kind;
    struct stow_session* session;
    uint32_t id;
    uint32_t value;
};

struct test_session
{
    struct stow_session session;
};

K_MSGQ_DEFINE(calls, sizeof(struct call), 32, sizeof(void*));
K_MSGQ_DEFINE(sent, sizeof(struct net_buf*), TEST_BUF_COUNT, sizeof(void*));
K_SEM_DEFINE(gate, 0, 1);
NET_BUF_POOL_DEFINE(send_pool, TEST_BUF_COUNT, TEST_BUF_SIZE, 0, NULL);
NET_BUF_POOL_DEFINE(message_pool, TEST_BUF_COUNT, TEST_BUF_SIZE, 0, NULL);

static int accept_result;
static atomic_t refuse_sends;
static atomic_t idles;
static atomic_t events_freed;
static k_timeout_t idle_wait;

static void record(enum call_kind kind, struct stow_session* session, uint32_t value)
{
    struct call call = { .kind = kind, .session = session, .id = session->id, .value = value };
    zassert_equal(k_msgq_put(&calls, &call, K_NO_WAIT), 0, "call log full");
}

static int accept_session(struct stow_session* session)
{
    record(CALL_ACCEPT, session, 0);
    return accept_result;
}

static void record_opened(struct stow_session* session) { record(CALL_OPENED, session, 0); }

static void record_closed(struct stow_session* session) { record(CALL_CLOSED, session, 0); }

static uint8_t record_answer(struct stow_session* session, const struct net_buf* msg, struct stow_msg_writer* reply)
{
    uint8_t tag = msg->data[0];
    record(CALL_ANSWER, session, tag);
    if (tag == TOO_LARGE)
    {
        (void)stow_msg_write(reply, &(struct stow_msg){ .code = STOW_MSG_DESCRIBE, .describe = { .named = true } });
        static const char name[TEST_BUF_SIZE + 1] = { [0 ... TEST_BUF_SIZE - 1] = 'n' };
        (void)stow_msg_put_name(reply, name);
        return STOW_TAG_MAX;
    }
    (void)stow_msg_write(reply, &(struct stow_msg){ .code = STOW_MSG_OK, .tag = tag });
    return tag;
}

static void record_receive(struct stow_session* session, const struct net_buf* msg)
{
    record(CALL_RECEIVE, session, msg->data[0]);
}

static void record_work(struct stow_session* session, uint32_t arg, struct net_buf* buf)
{
    record(CALL_WORK, session, (buf != NULL) ? buf->data[0] : arg);
    if (arg == GATE)
    {
        (void)k_sem_take(&gate, K_FOREVER);
    }
}

static void record_update(struct stow_session* session, event_t* event)
{
    record(CALL_UPDATE, session, event->type->id);
}

static k_timeout_t count_idle(void)
{
    atomic_inc(&idles);
    return idle_wait;
}

static void record_freed(struct stow_session* session) { record(CALL_FREED, session, 0); }

static void count_freed_event(event_t* event)
{
    ARG_UNUSED(event);
    atomic_inc(&events_freed);
}

DEFINE_EVENT_TYPE(TEST_EVENT_ID, test_event, count_freed_event);

static const struct stow_runtime_role answering_role = {
    .accept = accept_session,
    .opened = record_opened,
    .closed = record_closed,
    .answer = record_answer,
    .work = record_work,
    .update = record_update,
    .idle = count_idle,
    .freed = record_freed,
};

static const struct stow_runtime_role receiving_role = {
    .opened = record_opened,
    .closed = record_closed,
    .receive = record_receive,
};

static struct test_session answering_sessions[2];
static struct test_session receiving_sessions[1];
static struct test_session late_sessions[1];

STOW_RUNTIME_DEFINE(answering, answering_role, answering_sessions, session, TEST_QUEUE_SIZE, 2048, TEST_PRIORITY);
STOW_RUNTIME_DEFINE(receiving, receiving_role, receiving_sessions, session, TEST_QUEUE_SIZE, 2048, TEST_PRIORITY);
STOW_RUNTIME_DEFINE(late, receiving_role, late_sessions, session, TEST_QUEUE_SIZE, 2048, TEST_PRIORITY);

static const struct stow_endpoint answering_endpoint = STOW_RUNTIME_ENDPOINT(answering);
static const struct stow_endpoint other_endpoint = STOW_RUNTIME_ENDPOINT(answering);
static const struct stow_endpoint receiving_endpoint = STOW_RUNTIME_ENDPOINT(receiving);
static const struct stow_endpoint late_endpoint = STOW_RUNTIME_ENDPOINT(late);

static int capture_send(struct stow_transport* transport, uint32_t session_id, struct net_buf* buf)
{
    ARG_UNUSED(transport);
    ARG_UNUSED(session_id);
    if (atomic_get(&refuse_sends) != 0)
    {
        return -EIO;
    }
    return (k_msgq_put(&sent, &buf, K_NO_WAIT) == 0) ? SUCCESS : -ENOBUFS;
}

static struct stow_transport transport = {
    .send = capture_send,
    .pool = &send_pool,
};

static void open_gate(void) { k_sem_give(&gate); }

static int open_on(const struct stow_endpoint* on, uint32_t session_id)
{
    return on->session_opened(on, session_id, 0, &transport);
}

static void close_on(const struct stow_endpoint* on, uint32_t session_id) { on->session_closed(on, session_id); }

static struct net_buf* message(uint8_t first)
{
    struct net_buf* buf = net_buf_alloc(&message_pool, K_NO_WAIT);
    zassert_not_null(buf);
    net_buf_add_u8(buf, first);
    return buf;
}

static void deliver_message(const struct stow_endpoint* on, uint32_t session_id, uint8_t first)
{
    struct net_buf* buf = message(first);
    on->receive(on, session_id, buf);
    net_buf_unref(buf);
}

static int post(uint32_t session_id, uint32_t arg)
{
    return stow_runtime_post(&answering, &answering_endpoint, session_id, arg, NULL);
}

static struct call next_call(void)
{
    struct call call = { 0 };
    zassert_equal(k_msgq_get(&calls, &call, K_NO_WAIT), 0, "a call is missing");
    return call;
}

static struct call expect_call(enum call_kind kind, uint32_t session_id)
{
    struct call call = next_call();
    zassert_equal(call.kind, kind, "call %d instead of %d", call.kind, kind);
    zassert_equal(call.id, session_id, "call for session %u instead of %u", call.id, session_id);
    return call;
}

static void expect_no_call(void)
{
    struct call call = { 0 };
    zassert_not_equal(k_msgq_get(&calls, &call, K_NO_WAIT), 0, "unexpected call %d for session %u", call.kind, call.id);
}

/**
 * @brief Open a session and let it be opened
 */
static struct stow_session* open_session(uint32_t session_id)
{
    zassert_equal(open_on(&answering_endpoint, session_id), 0);
    test_settle();
    expect_call(CALL_ACCEPT, session_id);
    return expect_call(CALL_OPENED, session_id).session;
}

/**
 * @brief Block the thread inside work for an open session
 */
static void block_thread(uint32_t session_id)
{
    zassert_equal(post(session_id, GATE), 0);
    test_settle();
    expect_call(CALL_WORK, session_id);
}

static size_t free_buffers(struct net_buf_pool* pool)
{
    struct net_buf* taken[TEST_BUF_COUNT];
    size_t count = 0;
    while (count < ARRAY_SIZE(taken) && (taken[count] = net_buf_alloc(pool, K_NO_WAIT)) != NULL)
    {
        count++;
    }
    for (size_t i = 0; i < count; i++)
    {
        net_buf_unref(taken[i]);
    }
    return count;
}

static struct net_buf* next_sent(void)
{
    struct net_buf* buf = NULL;
    zassert_equal(k_msgq_get(&sent, &buf, K_NO_WAIT), 0, "nothing was sent");
    return buf;
}

static void before_each(void* fixture)
{
    ARG_UNUSED(fixture);
    accept_result = SUCCESS;
    idle_wait = K_FOREVER;
    atomic_clear(&refuse_sends);
    atomic_clear(&events_freed);
    (void)stow_runtime_start(&answering);
    (void)stow_runtime_start(&receiving);
}

static void after_each(void* fixture)
{
    ARG_UNUSED(fixture);
    idle_wait = K_FOREVER;
    for (uint32_t session_id = SESSION_A; session_id <= SESSION_C; session_id++)
    {
        close_on(&answering_endpoint, session_id);
        close_on(&other_endpoint, session_id);
        close_on(&receiving_endpoint, session_id);
        close_on(&late_endpoint, session_id);
    }
    test_settle();
    k_msgq_purge(&calls);
    struct net_buf* buf = NULL;
    while (k_msgq_get(&sent, &buf, K_NO_WAIT) == 0)
    {
        net_buf_unref(buf);
    }
    zassert_equal(free_buffers(&send_pool), TEST_BUF_COUNT, "a sent buffer leaked");
    zassert_equal(free_buffers(&message_pool), TEST_BUF_COUNT, "a message leaked");
    zassert_equal(open_on(&answering_endpoint, SESSION_A), 0, "a slot stayed taken");
    zassert_equal(open_on(&answering_endpoint, SESSION_B), 0, "a slot stayed taken");
    close_on(&answering_endpoint, SESSION_A);
    close_on(&answering_endpoint, SESSION_B);
    test_settle();
    k_msgq_purge(&calls);
}

ZTEST_SUITE(stow_runtime, NULL, NULL, before_each, after_each, NULL);

ZTEST(stow_runtime, test_starting_twice_is_refused) { zassert_equal(stow_runtime_start(&answering), -EALREADY); }

ZTEST(stow_runtime, test_a_session_opened_before_start_opens_once_started)
{
    zassert_equal(late_endpoint.session_opened(&late_endpoint, SESSION_A, 0, &transport), 0);
    deliver_message(&late_endpoint, SESSION_A, 6);

    zassert_equal(stow_runtime_start(&late), 0);
    test_settle();
    expect_call(CALL_OPENED, SESSION_A);
    zassert_equal(expect_call(CALL_RECEIVE, SESSION_A).value, 6);
    expect_no_call();
}

ZTEST(stow_runtime, test_an_open_is_refused_for_a_bad_transport_a_duplicate_or_a_full_table)
{
    struct stow_transport unusable = transport;
    unusable.send = NULL;
    zassert_equal(answering_endpoint.session_opened(&answering_endpoint, SESSION_A, 0, &unusable), -EINVAL);

    zassert_equal(open_on(&answering_endpoint, SESSION_A), 0);
    zassert_equal(open_on(&answering_endpoint, SESSION_A), -EALREADY);
    zassert_equal(open_on(&other_endpoint, SESSION_A), 0, "a session ID is unique only within its endpoint");
    zassert_equal(open_on(&answering_endpoint, SESSION_B), -ENOMEM);
}

ZTEST(stow_runtime, test_accept_can_refuse_an_open)
{
    accept_result = -EPERM;
    zassert_equal(open_on(&answering_endpoint, SESSION_A), -EPERM);
    deliver_message(&answering_endpoint, SESSION_A, 1);
    test_settle();
    expect_call(CALL_ACCEPT, SESSION_A);
    expect_no_call();

    accept_result = SUCCESS;
    open_session(SESSION_A);
    open_session(SESSION_B);
}

ZTEST(stow_runtime, test_opened_comes_before_anything_for_the_session)
{
    zassert_equal(open_on(&answering_endpoint, SESSION_A), 0);
    deliver_message(&answering_endpoint, SESSION_A, 7);
    zassert_equal(post(SESSION_A, 8), 0);
    zassert_equal(receiving_endpoint.session_opened(&receiving_endpoint, SESSION_A, 0, &transport), 0);
    deliver_message(&receiving_endpoint, SESSION_A, 9);
    test_settle();

    expect_call(CALL_ACCEPT, SESSION_A);
    expect_call(CALL_OPENED, SESSION_A);
    zassert_equal(expect_call(CALL_ANSWER, SESSION_A).value, 7);
    zassert_equal(expect_call(CALL_WORK, SESSION_A).value, 8);
    expect_call(CALL_OPENED, SESSION_A);
    zassert_equal(expect_call(CALL_RECEIVE, SESSION_A).value, 9);
    expect_no_call();
}

ZTEST(stow_runtime, test_a_session_opened_while_the_queue_drains_is_opened_before_its_message)
{
    open_session(SESSION_A);
    block_thread(SESSION_A);

    zassert_equal(open_on(&answering_endpoint, SESSION_B), 0);
    deliver_message(&answering_endpoint, SESSION_B, 5);
    expect_call(CALL_ACCEPT, SESSION_B);
    open_gate();
    test_settle();

    expect_call(CALL_OPENED, SESSION_B);
    zassert_equal(expect_call(CALL_ANSWER, SESSION_B).value, 5);
    expect_no_call();
}

ZTEST(stow_runtime, test_closed_is_called_only_for_an_opened_session)
{
    open_session(SESSION_A);
    close_on(&answering_endpoint, SESSION_A);
    test_settle();
    expect_call(CALL_CLOSED, SESSION_A);
    expect_call(CALL_FREED, SESSION_A);
    expect_no_call();
}

ZTEST(stow_runtime, test_a_session_closed_before_it_opened_gets_neither_call)
{
    zassert_equal(open_on(&answering_endpoint, SESSION_A), 0);
    deliver_message(&answering_endpoint, SESSION_A, 1);
    close_on(&answering_endpoint, SESSION_A);
    test_settle();

    expect_call(CALL_ACCEPT, SESSION_A);
    expect_call(CALL_FREED, SESSION_A);
    expect_no_call();
}

ZTEST(stow_runtime, test_open_and_close_survive_a_full_queue)
{
    open_session(SESSION_A);
    block_thread(SESSION_A);
    while (post(SESSION_A, 1) == SUCCESS)
    {
    }

    close_on(&answering_endpoint, SESSION_A);
    zassert_equal(open_on(&answering_endpoint, SESSION_B), 0);
    expect_call(CALL_ACCEPT, SESSION_B);
    open_gate();
    test_settle();

    expect_call(CALL_CLOSED, SESSION_A);
    expect_call(CALL_FREED, SESSION_A);
    expect_call(CALL_OPENED, SESSION_B);
    expect_no_call();
}

ZTEST(stow_runtime, test_entries_for_an_earlier_session_in_the_slot_are_dropped)
{
    open_session(SESSION_A);
    open_session(SESSION_B);
    block_thread(SESSION_B);
    zassert_equal(post(SESSION_B, GATE), 0);
    zassert_equal(post(SESSION_A, 1), 0);
    deliver_message(&answering_endpoint, SESSION_A, 2);
    close_on(&answering_endpoint, SESSION_A);
    open_gate();
    test_settle();
    expect_call(CALL_CLOSED, SESSION_A);
    expect_call(CALL_FREED, SESSION_A);
    expect_call(CALL_WORK, SESSION_B);

    zassert_equal(open_on(&answering_endpoint, SESSION_A), 0);
    expect_call(CALL_ACCEPT, SESSION_A);
    open_gate();
    test_settle();

    expect_call(CALL_OPENED, SESSION_A);
    expect_no_call();
    zassert_equal(k_msgq_num_used_get(&sent), 0, "a message from the earlier session was answered");
}

ZTEST(stow_runtime, test_post_refuses_a_closed_session_and_a_full_queue_and_releases_the_buffer)
{
    zassert_equal(stow_runtime_post(&answering, &answering_endpoint, SESSION_A, 0, message(1)), -ENOTCONN);
    zassert_equal(free_buffers(&message_pool), TEST_BUF_COUNT);

    open_session(SESSION_A);
    block_thread(SESSION_A);
    for (int i = 0; i < TEST_QUEUE_SIZE; i++)
    {
        zassert_equal(stow_runtime_post(&answering, &answering_endpoint, SESSION_A, 0, message(i)), 0);
    }
    zassert_equal(stow_runtime_post(&answering, &answering_endpoint, SESSION_A, 0, message(TEST_QUEUE_SIZE)), -ENOBUFS);
    zassert_equal(free_buffers(&message_pool), TEST_BUF_COUNT - TEST_QUEUE_SIZE);
    open_gate();
    test_settle();

    for (int i = 0; i < TEST_QUEUE_SIZE; i++)
    {
        zassert_equal(expect_call(CALL_WORK, SESSION_A).value, i, "work lent the wrong buffer");
    }
    expect_no_call();
}

ZTEST(stow_runtime, test_broadcast_reaches_each_opened_session_once)
{
    open_session(SESSION_A);
    open_session(SESSION_B);
    close_on(&answering_endpoint, SESSION_B);

    event_t* event = NULL;
    zassert_equal(EVENT_ALLOC(&test_event, 0, &event), 0);
    zassert_equal(stow_runtime_broadcast(&answering, event), 0);
    test_settle();

    expect_call(CALL_CLOSED, SESSION_B);
    expect_call(CALL_FREED, SESSION_B);
    zassert_equal(expect_call(CALL_UPDATE, SESSION_A).value, TEST_EVENT_ID);
    expect_no_call();
    zassert_equal(atomic_get(&events_freed), 1, "the event was not released after the last update");
}

ZTEST(stow_runtime, test_broadcast_releases_the_event_when_the_queue_is_full)
{
    open_session(SESSION_A);
    block_thread(SESSION_A);
    while (post(SESSION_A, 1) == SUCCESS)
    {
    }

    event_t* event = NULL;
    zassert_equal(EVENT_ALLOC(&test_event, 0, &event), 0);
    zassert_equal(stow_runtime_broadcast(&answering, event), -ENOBUFS);
    zassert_equal(atomic_get(&events_freed), 1);
    open_gate();
}

ZTEST(stow_runtime, test_a_reference_keeps_the_slot_until_freed)
{
    struct stow_session* session = stow_session_ref(open_session(SESSION_A));
    open_session(SESSION_B);
    close_on(&answering_endpoint, SESSION_A);
    test_settle();
    expect_call(CALL_CLOSED, SESSION_A);
    expect_no_call();

    zassert_equal(open_on(&answering_endpoint, SESSION_C), -ENOMEM, "a referenced slot was reused");
    stow_session_unref(&session);
    zassert_is_null(session);
    expect_call(CALL_FREED, SESSION_A);
    expect_no_call();
    zassert_equal(open_on(&answering_endpoint, SESSION_C), 0);
}

ZTEST(stow_runtime, test_the_answer_is_sent_with_the_tag_the_role_returns)
{
    open_session(SESSION_A);
    deliver_message(&answering_endpoint, SESSION_A, 3);
    test_settle();
    expect_call(CALL_ANSWER, SESSION_A);

    struct net_buf* buf = next_sent();
    uint32_t code = STOW_MSG_ABSENT;
    uint8_t tag = STOW_TAG_NONE;
    zassert_equal(stow_msg_peek(buf->data, buf->len, &code, &tag), 0);
    zassert_equal(code, STOW_MSG_OK);
    zassert_equal(tag, 3);
    net_buf_unref(buf);
}

ZTEST(stow_runtime, test_an_answer_that_does_not_fit_becomes_an_error)
{
    open_session(SESSION_A);
    deliver_message(&answering_endpoint, SESSION_A, TOO_LARGE);
    test_settle();
    expect_call(CALL_ANSWER, SESSION_A);

    struct net_buf* buf = next_sent();
    struct stow_msg_reader reader;
    struct stow_msg msg;
    const struct stow_msg_items items = { 0 };
    zassert_equal(stow_msg_read(&reader, buf->data, buf->len, &items, &msg), 0);
    zassert_equal(msg.code, STOW_MSG_ERROR);
    zassert_equal(msg.tag, STOW_TAG_MAX);
    zassert_equal(msg.error.err, -E2BIG);
    net_buf_unref(buf);
}

ZTEST(stow_runtime, test_a_refused_answer_is_released)
{
    open_session(SESSION_A);
    atomic_set(&refuse_sends, 1);
    deliver_message(&answering_endpoint, SESSION_A, 4);
    test_settle();
    expect_call(CALL_ANSWER, SESSION_A);
    zassert_equal(k_msgq_num_used_get(&sent), 0);
    zassert_equal(free_buffers(&send_pool), TEST_BUF_COUNT, "a refused answer leaked");
}

ZTEST(stow_runtime, test_idle_wakes_the_thread)
{
    idle_wait = K_MSEC(10);
    open_session(SESSION_A);
    atomic_val_t before = atomic_get(&idles);
    k_msleep(50);
    zassert_true(atomic_get(&idles) > before + 1, "idle did not wake the thread");

    idle_wait = K_FOREVER;
    k_msleep(20);
    before = atomic_get(&idles);
    k_msleep(50);
    zassert_equal(atomic_get(&idles), before, "the thread woke without a timeout");
}
