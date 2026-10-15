/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "test_common.h"

static struct test_link link;
static struct stow_client_peer* peer;

static void before_each(void* fixture)
{
    ARG_UNUSED(fixture);
    test_reset();
    peer = test_connect(&link, &test_peer_client, STOW_ROLE_GUEST);
}

static void after_each(void* fixture)
{
    ARG_UNUSED(fixture);
    test_disconnect(&link, &peer);
}

ZTEST_SUITE(stow_client_loopback, NULL, NULL, before_each, after_each, NULL);

ZTEST(stow_client_loopback, test_a_connected_peer_reports_its_items_and_limits)
{
    for (uint16_t id = 0; id < PEER_ID_COUNT; id++)
    {
        enum stow_client_availability expected =
            (id == PEER_ID_FIRMWARE_VERSION) ? STOW_CLIENT_MISSING : STOW_CLIENT_AVAILABLE;
        zassert_equal(stow_client_availability(peer, id), expected, "item %u", id);
    }

    struct stow_client_peer_info info;
    zassert_equal(stow_client_peer_info(peer, &info), 0);
    zassert_equal(info.max_message, TEST_BUF_SIZE);
    zassert_equal(info.roles, PEER_ROLE_GUEST);
}

ZTEST(stow_client_loopback, test_int_float_and_enum_round_trip)
{
    zassert_equal(
        stow_client_set_one(peer, PEER_ID_COUNTER, (data_value_t){ .type = STOW_ITEM_TYPE_INT, .data.int_value = -42 }),
        0);
    zassert_equal(
        stow_client_set_one(
            peer, PEER_ID_RATIO, (data_value_t){ .type = STOW_ITEM_TYPE_FLOAT, .data.float_value = 2.25f }),
        0);
    zassert_equal(
        stow_client_set_one(
            peer, PEER_ID_MODE, (data_value_t){ .type = STOW_ITEM_TYPE_ENUM, .data.int_value = peer_Mode_AUTO }),
        0);

    struct stow_client_value value;
    zassert_equal(stow_client_get_one(peer, PEER_ID_COUNTER, &value), 0);
    zassert_equal(value.value.data.int_value, -42);
    stow_client_release(&test_peer_client, &value);
    zassert_equal(stow_client_get_one(peer, PEER_ID_RATIO, &value), 0);
    zassert_equal(value.value.data.float_value, 2.25f);
    stow_client_release(&test_peer_client, &value);
    zassert_equal(stow_client_get_one(peer, PEER_ID_MODE, &value), 0);
    zassert_equal(value.value.data.int_value, peer_Mode_AUTO);
    stow_client_release(&test_peer_client, &value);

    data_value_t local;
    zassert_equal(stow_get(STOW_ID_COUNTER, &local), 0);
    zassert_equal(local.data.int_value, -42);
}

ZTEST(stow_client_loopback, test_string_bytes_and_buffer_round_trip)
{
    uint32_t blocks = test_blocks_in_use();

    zassert_equal(
        stow_client_set_one(
            peer, PEER_ID_LABEL, (data_value_t){ .type = STOW_ITEM_TYPE_STRING, .data.string_value = "remote" }),
        0);

    buffer_t* code = NULL;
    zassert_equal(mem_alloc(sizeof(buffer_t) + 4, (void**)&code), 0);
    code->len = 4;
    memcpy(code->buf, "\x0A\x0B\x0C\x0D", 4);
    zassert_equal(
        stow_client_set_one(
            peer, PEER_ID_CODE, (data_value_t){ .type = STOW_ITEM_TYPE_BYTE_ARRAY, .data.buffer_value = code }),
        0);
    zassert_equal(
        stow_client_set_one(
            peer, PEER_ID_BLOB, (data_value_t){ .type = STOW_ITEM_TYPE_BUFFER, .data.buffer_value = code }),
        0);
    void* block = code;
    mem_unref(&block);

    struct stow_client_value value;
    zassert_equal(stow_client_get_one(peer, PEER_ID_LABEL, &value), 0);
    zassert_equal(value.id, PEER_ID_LABEL);
    zassert_str_equal(value.value.data.string_value, "remote");
    stow_client_release(&test_peer_client, &value);

    zassert_equal(stow_client_get_one(peer, PEER_ID_CODE, &value), 0);
    zassert_equal(value.value.data.buffer_value->len, 4);
    zassert_mem_equal(value.value.data.buffer_value->buf, "\x0A\x0B\x0C\x0D", 4);
    stow_client_release(&test_peer_client, &value);

    zassert_equal(stow_client_get_one(peer, PEER_ID_BLOB, &value), 0);
    zassert_equal(value.value.data.buffer_value->len, 4);
    stow_client_release(&test_peer_client, &value);

    zassert_equal(test_blocks_in_use(), blocks, "values leaked");
}

ZTEST(stow_client_loopback, test_struct_round_trip)
{
    uint32_t blocks = test_blocks_in_use();
    peer_Point_t point = { .x = 3, .y = -4, .label = "here" };
    zassert_equal(
        stow_client_set_one(
            peer, PEER_ID_POSITION, (data_value_t){ .type = STOW_ITEM_TYPE_STRUCT, .data.raw_value = &point }),
        0);

    struct stow_client_value value;
    zassert_equal(stow_client_get_one(peer, PEER_ID_POSITION, &value), 0);
    const peer_Point_t* got = value.value.data.raw_value;
    zassert_equal(got->x, 3);
    zassert_equal(got->y, -4);
    zassert_str_equal(got->label, "here");
    stow_client_release(&test_peer_client, &value);

    data_value_t local;
    zassert_equal(stow_get(STOW_ID_POSITION, &local), 0);
    zassert_equal(((const Point_t*)local.data.raw_value)->y, -4);
    stow_release(STOW_ID_POSITION, &local);

    zassert_equal(test_blocks_in_use(), blocks, "values leaked");
}

ZTEST(stow_client_loopback, test_an_item_too_large_to_describe_is_incompatible)
{
    test_disconnect(&link, &peer);
    test_server_headroom = TEST_BUF_SIZE - 128;
    peer = test_connect(&link, &test_peer_client, STOW_ROLE_GUEST);

    zassert_equal(stow_client_availability(peer, PEER_ID_SHADE), STOW_CLIENT_INCOMPATIBLE);
    zassert_equal(stow_client_availability(peer, PEER_ID_POSITION), STOW_CLIENT_AVAILABLE);
    zassert_equal(stow_client_availability(peer, PEER_ID_COUNTER), STOW_CLIENT_AVAILABLE);

    struct stow_client_value value;
    zassert_equal(stow_client_get_one(peer, PEER_ID_COUNTER, &value), 0);
    stow_client_release(&test_peer_client, &value);
}

ZTEST(stow_client_loopback, test_several_items_in_one_request)
{
    const uint16_t ids[] = { PEER_ID_COUNTER, PEER_ID_MODE, PEER_ID_LOCKED };
    const data_value_t values[] = {
        { .type = STOW_ITEM_TYPE_INT, .data.int_value = 7 },
        { .type = STOW_ITEM_TYPE_ENUM, .data.int_value = peer_Mode_ON },
    };
    zassert_equal(stow_client_set(peer, ids, values, 2), 0);

    struct stow_client_value got[3];
    zassert_equal(stow_client_get(peer, ids, 3, got), 0);
    zassert_equal(got[0].value.data.int_value, 7);
    zassert_equal(got[1].id, PEER_ID_MODE);
    zassert_equal(got[1].value.data.int_value, peer_Mode_ON);
    zassert_equal(got[2].value.data.int_value, 3);
    for (size_t i = 0; i < ARRAY_SIZE(got); i++)
    {
        struct stow_client_value* entry = &got[i];
        stow_client_release(&test_peer_client, entry);
    }
}

ZTEST(stow_client_loopback, test_values_are_checked_before_sending)
{
    zassert_equal(
        stow_client_set_one(peer, PEER_ID_COUNTER, (data_value_t){ .type = STOW_ITEM_TYPE_INT, .data.int_value = 101 }),
        -EINVAL);
    zassert_equal(
        stow_client_set_one(
            peer, PEER_ID_COUNTER, (data_value_t){ .type = STOW_ITEM_TYPE_FLOAT, .data.float_value = 1.0f }),
        -EINVAL);
    zassert_equal(stow_client_set_one(peer, PEER_ID_COUNT, (data_value_t){ .type = STOW_ITEM_TYPE_INT }), -EINVAL);
    zassert_equal(stow_client_get(peer, NULL, 0, NULL), -EINVAL);
}

ZTEST(stow_client_loopback, test_peer_errors_are_passed_through)
{
    zassert_equal(
        stow_client_set_one(peer, PEER_ID_LOCKED, (data_value_t){ .type = STOW_ITEM_TYPE_INT, .data.int_value = 1 }),
        -EACCES);

    zassert_equal(
        stow_client_set_one(
            peer, PEER_ID_ONCE, (data_value_t){ .type = STOW_ITEM_TYPE_STRING, .data.string_value = "a" }),
        0);
    zassert_equal(
        stow_client_set_one(
            peer, PEER_ID_ONCE, (data_value_t){ .type = STOW_ITEM_TYPE_STRING, .data.string_value = "b" }),
        -EALREADY);

    const uint16_t ids[] = { PEER_ID_COUNTER, PEER_ID_LOCKED };
    const data_value_t values[] = {
        { .type = STOW_ITEM_TYPE_INT, .data.int_value = 9 },
        { .type = STOW_ITEM_TYPE_INT, .data.int_value = 1 },
    };
    struct test_async async;
    test_async_init(&async);
    zassert_equal(stow_client_set_async(peer, ids, values, 2, test_capture_cb, &async), 0);
    zassert_equal(k_sem_take(&async.done, TEST_WAIT), 0);
    zassert_equal(async.err, -EACCES);
    zassert_equal(async.index, 1, "the first entry was applied");

    struct stow_client_value value;
    zassert_equal(stow_client_get_one(peer, PEER_ID_COUNTER, &value), 0);
    zassert_equal(value.value.data.int_value, 9);
    stow_client_release(&test_peer_client, &value);
}

ZTEST(stow_client_loopback, test_requests_beyond_the_peer_limits_fail)
{
    buffer_t* blob = NULL;
    zassert_equal(mem_alloc(sizeof(buffer_t) + TEST_BUF_SIZE / 2, (void**)&blob), 0);
    blob->len = TEST_BUF_SIZE / 2;
    memset(blob->buf, 0x5A, blob->len);

    const uint16_t ids[] = { PEER_ID_BLOB, PEER_ID_BLOB };
    const data_value_t values[] = {
        { .type = STOW_ITEM_TYPE_BUFFER, .data.buffer_value = blob },
        { .type = STOW_ITEM_TYPE_BUFFER, .data.buffer_value = blob },
    };
    zassert_equal(stow_client_set(peer, ids, values, 2), -EMSGSIZE);
    zassert_equal(stow_client_set(peer, ids, values, 1), 0);
    void* block = blob;
    mem_unref(&block);

    struct stow_client_value got[2];
    zassert_equal(stow_client_get(peer, ids, 2, got), -E2BIG);
}

ZTEST(stow_client_loopback, test_subscribed_updates_arrive_as_events_until_unsubscribed)
{
    uint32_t blocks = test_blocks_in_use();
    const uint16_t ids[] = { PEER_ID_COUNTER, PEER_ID_POSITION };
    zassert_equal(stow_client_subscribe(peer, ids, 2), 0);

    event_t* event = NULL;
    zassert_equal(stow_set(STOW_ID_COUNTER, (data_value_t){ .type = STOW_ITEM_TYPE_INT, .data.int_value = 55 }), 0);
    zassert_equal(test_next_update(TEST_WAIT, &event), 0);
    const struct stow_client_update_payload* update = (const struct stow_client_update_payload*)event->data.buf;
    zassert_equal_ptr(update->peer, peer);
    zassert_equal(update->value.id, PEER_ID_COUNTER);
    zassert_equal(update->value.value.data.int_value, 55);
    event_unref(&event);

    Point_t point = { .x = 1, .y = 2, .label = "kept" };
    zassert_equal(
        stow_set(STOW_ID_POSITION, (data_value_t){ .type = STOW_ITEM_TYPE_STRUCT, .data.raw_value = &point }), 0);
    zassert_equal(test_next_update(TEST_WAIT, &event), 0);
    test_settle();
    update = (const struct stow_client_update_payload*)event->data.buf;
    zassert_equal(update->value.id, PEER_ID_POSITION);
    zassert_str_equal(((const peer_Point_t*)update->value.value.data.raw_value)->label, "kept");
    event_unref(&event);

    zassert_equal(stow_client_unsubscribe(peer, ids, 2), 0);
    zassert_equal(stow_set(STOW_ID_COUNTER, (data_value_t){ .type = STOW_ITEM_TYPE_INT, .data.int_value = 56 }), 0);
    zassert_not_equal(test_next_update(K_MSEC(100), &event), 0, "Update after unsubscribing");
    zassert_equal(test_blocks_in_use(), blocks, "event or value leaked");
}

ZTEST(stow_client_loopback, test_a_value_kept_with_a_reference_outlives_its_event)
{
    uint32_t blocks = test_blocks_in_use();
    zassert_equal(stow_client_subscribe_one(peer, PEER_ID_POSITION), 0);
    Point_t point = { .x = 3, .y = 4, .label = "ref" };
    zassert_equal(
        stow_set(STOW_ID_POSITION, (data_value_t){ .type = STOW_ITEM_TYPE_STRUCT, .data.raw_value = &point }), 0);

    event_t* event = NULL;
    zassert_equal(test_next_update(TEST_WAIT, &event), 0);
    struct stow_client_value kept = ((const struct stow_client_update_payload*)event->data.buf)->value;
    stow_client_ref(&test_peer_client, &kept);
    event_unref(&event);

    zassert_str_equal(((const peer_Point_t*)kept.value.data.raw_value)->label, "ref");
    stow_client_release(&test_peer_client, &kept);
    zassert_equal(stow_client_unsubscribe_one(peer, PEER_ID_POSITION), 0);
    zassert_equal(test_blocks_in_use(), blocks, "event or value leaked");
}

ZTEST(stow_client_loopback, test_an_async_get_delivers_its_values_in_a_result_event)
{
    uint32_t blocks = test_blocks_in_use();
    zassert_equal(stow_set(STOW_ID_COUNTER, (data_value_t){ .type = STOW_ITEM_TYPE_INT, .data.int_value = 12 }), 0);
    Point_t point = { .x = 5, .y = 6, .label = "async" };
    zassert_equal(
        stow_set(STOW_ID_POSITION, (data_value_t){ .type = STOW_ITEM_TYPE_STRUCT, .data.raw_value = &point }), 0);

    const uint16_t ids[] = { PEER_ID_COUNTER, PEER_ID_POSITION };
    struct test_async async;
    test_async_init(&async);
    zassert_equal(stow_client_get_async(peer, ids, 2, test_capture_cb, &async), 0);
    zassert_equal(k_sem_take(&async.done, TEST_WAIT), 0);
    test_settle();
    zassert_equal(async.err, 0);
    zassert_equal(async.count, 2);
    zassert_equal(test_async_values(&async)[0].value.data.int_value, 12);
    zassert_str_equal(((const peer_Point_t*)test_async_values(&async)[1].value.data.raw_value)->label, "async");
    test_async_release(&async);

    zassert_equal(test_blocks_in_use(), blocks, "event or value leaked");
}

ZTEST(stow_client_loopback, test_an_event_kept_after_its_peer_closes_is_released_through_that_peer)
{
    uint32_t blocks = test_blocks_in_use();
    struct test_link other_link;
    struct stow_client_peer* other = test_connect(&other_link, &test_peer_client, STOW_ROLE_GUEST);
    zassert_equal(stow_client_subscribe_one(other, PEER_ID_POSITION), 0);
    Point_t point = { .x = 7, .y = 8, .label = "closed" };
    zassert_equal(
        stow_set(STOW_ID_POSITION, (data_value_t){ .type = STOW_ITEM_TYPE_STRUCT, .data.raw_value = &point }), 0);

    event_t* event = NULL;
    zassert_equal(test_next_update(TEST_WAIT, &event), 0);
    test_disconnect(&other_link, &other);

    const struct stow_client_update_payload* update = (const struct stow_client_update_payload*)event->data.buf;
    zassert_equal(stow_client_peer_session_id(update->peer), other_link.session_id);
    zassert_str_equal(((const peer_Point_t*)update->value.value.data.raw_value)->label, "closed");
    event_unref(&event);

    zassert_equal(test_blocks_in_use(), blocks, "event, value or peer leaked");
}

ZTEST(stow_client_loopback, test_disconnected_peer_refuses_requests)
{
    test_close_client(&link);
    struct test_hook_event event = test_next_hook(TEST_WAIT);
    zassert_equal(event.hook, TEST_HOOK_DISCONNECTED);

    struct stow_client_value value;
    zassert_equal(stow_client_get_one(peer, PEER_ID_COUNTER, &value), -ENOTCONN);
    zassert_equal(stow_client_availability(peer, PEER_ID_COUNTER), STOW_CLIENT_MISSING);
    struct stow_client_peer_info info;
    zassert_equal(stow_client_peer_info(peer, &info), -ENOTCONN);
}

ZTEST(stow_client_loopback, test_several_peers_at_once)
{
    struct test_link second_link;
    struct stow_client_peer* second = test_connect(&second_link, &test_peer_client, STOW_ROLE_DEV);

    uint16_t id = PEER_ID_LOCKED;
    data_value_t value = { .type = STOW_ITEM_TYPE_INT, .data.int_value = 8 };
    zassert_equal(stow_client_set(peer, &id, &value, 1), -EACCES);
    zassert_equal(stow_client_set(second, &id, &value, 1), 0);
    struct stow_client_value got;
    zassert_equal(stow_client_get_one(peer, PEER_ID_LOCKED, &got), 0);
    zassert_equal(got.value.data.int_value, 8);
    stow_client_release(&test_peer_client, &got);

    test_disconnect(&second_link, &second);
}

ZTEST(stow_client_loopback, test_a_peer_slot_is_reused_only_once_released)
{
    struct test_link links[CONFIG_STOW_CLIENT_MAX_PEERS - 1];
    struct stow_client_peer* others[ARRAY_SIZE(links)];
    for (size_t i = 0; i < ARRAY_SIZE(links); i++)
    {
        others[i] = test_connect(&links[i], &test_peer_client, STOW_ROLE_GUEST);
    }
    struct test_link extra;
    test_link_init(&extra, &test_peer_client);
    zassert_equal(test_open_client(&extra), -ENOMEM);

    stow_protocol_endpoint.session_closed(&stow_protocol_endpoint, links[0].session_id);
    test_close_client(&links[0]);
    zassert_equal(test_next_hook(TEST_WAIT).hook, TEST_HOOK_DISCONNECTED);
    test_settle();
    zassert_equal(test_open_client(&extra), -ENOMEM, "slot reused while referenced");
    struct stow_client_peer_info info;
    zassert_equal(stow_client_peer_info(others[0], &info), -ENOTCONN);

    struct stow_client_peer* closed = others[0];
    stow_client_peer_unref(&closed);
    zassert_is_null(closed);
    zassert_equal(test_open_client(&extra), 0);
    test_close_client(&extra);
    test_settle();

    for (size_t i = 1; i < ARRAY_SIZE(links); i++)
    {
        test_disconnect(&links[i], &others[i]);
    }
}
