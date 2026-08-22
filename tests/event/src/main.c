/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <gantry/error.h>
#include <zephyr/ztest.h>

#include "event.c"

struct test_payload
{
    int value;
};

static int free_call_count;

static void on_free_counter(event_t* event)
{
    ARG_UNUSED(event);
    free_call_count++;
}

DECLARE_EVENT_TYPE(test_event_no_free);
DEFINE_EVENT_TYPE(1, test_event_no_free, NULL);

DECLARE_EVENT_TYPE(test_event_with_free);
DEFINE_EVENT_TYPE(2, test_event_with_free, on_free_counter);

static void reset_free_call_count(void* fixture)
{
    ARG_UNUSED(fixture);
    free_call_count = 0;
}

// ---------------------------------------------------------------------------
// Suite: event_alloc
// ---------------------------------------------------------------------------

ZTEST_SUITE(event_alloc, NULL, NULL, NULL, NULL, NULL);

ZTEST(event_alloc, test_alloc_basic)
{
    event_t* event = NULL;
    int ret = EVENT_ALLOC(&test_event_no_free, sizeof(struct test_payload), &event);

    zassert_equal(ret, SUCCESS);
    zassert_not_null(event);
    zassert_equal(event->type, &test_event_no_free);
    zassert_equal(event->data.len, sizeof(struct test_payload));
    zassert_is_null(event->next_event);

    EVENT_UNREF(&event);
}

ZTEST(event_alloc, test_alloc_zero_payload)
{
    event_t* event = NULL;
    int ret = EVENT_ALLOC(&test_event_no_free, 0, &event);

    zassert_equal(ret, SUCCESS);
    zassert_not_null(event);
    zassert_equal(event->data.len, 0);

    EVENT_UNREF(&event);
}

ZTEST(event_alloc, test_alloc_payload_is_writable)
{
    event_t* event = NULL;
    EVENT_ALLOC(&test_event_no_free, sizeof(struct test_payload), &event);

    struct test_payload* payload = (struct test_payload*)event->data.buf;
    payload->value = 42;

    zassert_equal(((struct test_payload*)event->data.buf)->value, 42);

    EVENT_UNREF(&event);
}

ZTEST(event_alloc, test_alloc_starts_with_ref_count_one)
{
    event_t* event = NULL;
    EVENT_ALLOC(&test_event_no_free, 0, &event);

    zassert_equal(mem_get_ref_count(event), 1);

    EVENT_UNREF(&event);
}

ZTEST(event_alloc, test_alloc_null_event_ptr)
{
    int ret = EVENT_ALLOC(&test_event_no_free, 0, NULL);
    zassert_equal(ret, -EINVAL);
}

ZTEST(event_alloc, test_alloc_non_null_event_ptr)
{
    event_t* event = NULL;
    EVENT_ALLOC(&test_event_no_free, 0, &event);
    zassert_not_null(event);

    // event now points to a live event; the pointer must be NULL before allocating into it
    int ret = EVENT_ALLOC(&test_event_no_free, 0, &event);
    zassert_equal(ret, -ENOTEMPTY);

    EVENT_UNREF(&event);
}

ZTEST(event_alloc, test_alloc_pool_exhausted)
{
    event_t* events[32] = { 0 };
    int count = 0;
    int ret = SUCCESS;

    while (ret == SUCCESS && count < (int)ARRAY_SIZE(events))
    {
        ret = EVENT_ALLOC(&test_event_no_free, sizeof(struct test_payload), &events[count]);
        if (ret == SUCCESS)
        {
            count++;
        }
    }

    zassert_equal(ret, -ENOMEM);

    for (int i = 0; i < count; i++)
    {
        EVENT_UNREF(&events[i]);
    }
}

// ---------------------------------------------------------------------------
// Suite: event_ref_unref
// ---------------------------------------------------------------------------

ZTEST_SUITE(event_ref_unref, NULL, NULL, NULL, NULL, NULL);

ZTEST(event_ref_unref, test_ref_increments_count)
{
    event_t* event = NULL;
    EVENT_ALLOC(&test_event_no_free, 0, &event);

    EVENT_REF(event);
    zassert_equal(mem_get_ref_count(event), 2);

    EVENT_UNREF(&event);
    EVENT_UNREF(&event);
}

ZTEST(event_ref_unref, test_unref_decrements_count)
{
    event_t* event = NULL;
    EVENT_ALLOC(&test_event_no_free, 0, &event);
    EVENT_REF(event);
    zassert_equal(mem_get_ref_count(event), 2);

    EVENT_UNREF(&event);
    zassert_equal(mem_get_ref_count(event), 1);

    EVENT_UNREF(&event);
}

ZTEST(event_ref_unref, test_unref_frees_at_zero)
{
    event_t* event = NULL;
    EVENT_ALLOC(&test_event_no_free, 0, &event);

    uint32_t used_before, total, used_after;
    mem_get_pool_usage(0, &used_before, &total);

    EVENT_UNREF(&event);

    mem_get_pool_usage(0, &used_after, &total);
    zassert_equal(used_after, used_before - 1);
}

ZTEST(event_ref_unref, test_unref_nulls_pointer_on_final_release)
{
    event_t* event = NULL;
    EVENT_ALLOC(&test_event_no_free, 0, &event);

    EVENT_UNREF(&event);

    zassert_is_null(event, "event pointer is not null after released");
}

ZTEST(event_ref_unref, test_unref_leaves_pointer_valid_while_references_remain)
{
    event_t* event = NULL;
    EVENT_ALLOC(&test_event_no_free, 0, &event);
    EVENT_REF(event);

    EVENT_UNREF(&event);

    zassert_not_null(event, "event pointer should not be nullified until release");
    zassert_equal(mem_get_ref_count(event), 1);

    EVENT_UNREF(&event);
    zassert_is_null(event);
}

ZTEST(event_ref_unref, test_unref_null_ptr_no_op)
{
    event_t* event = NULL;
    // Should not assert or crash
    EVENT_UNREF(&event);
    zassert_is_null(event);
}

// ---------------------------------------------------------------------------
// Suite: event_on_free
// ---------------------------------------------------------------------------

ZTEST_SUITE(event_on_free, NULL, NULL, reset_free_call_count, NULL, NULL);

ZTEST(event_on_free, test_on_free_called_when_last_ref_released)
{
    event_t* event = NULL;
    EVENT_ALLOC(&test_event_with_free, 0, &event);

    EVENT_UNREF(&event);

    zassert_equal(free_call_count, 1);
}

ZTEST(event_on_free, test_on_free_not_called_while_still_referenced)
{
    event_t* event = NULL;
    EVENT_ALLOC(&test_event_with_free, 0, &event);
    EVENT_REF(event);

    EVENT_UNREF(&event);
    zassert_equal(free_call_count, 0);

    EVENT_UNREF(&event);
    zassert_equal(free_call_count, 1);
}

ZTEST(event_on_free, test_on_free_not_called_when_type_has_none)
{
    event_t* event = NULL;
    EVENT_ALLOC(&test_event_no_free, 0, &event);

    // Must not crash despite a NULL on_free
    EVENT_UNREF(&event);
    zassert_equal(free_call_count, 0);
}

// ---------------------------------------------------------------------------
// Suite: event chains
// ---------------------------------------------------------------------------

ZTEST_SUITE(event_link, NULL, NULL, reset_free_call_count, NULL, NULL);

ZTEST(event_link, test_ref_increments_every_linked_event)
{
    event_t* head = NULL;
    event_t* tail = NULL;
    EVENT_ALLOC(&test_event_no_free, 0, &head);
    EVENT_ALLOC(&test_event_no_free, 0, &tail);
    head->next_event = tail;

    EVENT_REF(head);

    zassert_equal(mem_get_ref_count(head), 2);
    zassert_equal(mem_get_ref_count(tail), 2);

    EVENT_UNREF(&head);
    EVENT_UNREF(&head);
}

ZTEST(event_link, test_unref_decrements_every_linked_event)
{
    event_t* head = NULL;
    event_t* tail = NULL;
    EVENT_ALLOC(&test_event_no_free, 0, &head);
    EVENT_ALLOC(&test_event_no_free, 0, &tail);
    head->next_event = tail;
    EVENT_REF(head);

    EVENT_UNREF(&head);

    zassert_not_null(head, "chain must survive a release that isn't the last reference");
    zassert_equal(mem_get_ref_count(head), 1);
    zassert_equal(mem_get_ref_count(tail), 1);

    EVENT_UNREF(&head);
}

ZTEST(event_link, test_unref_frees_whole_chain_when_all_reach_zero)
{
    event_t* head = NULL;
    event_t* tail = NULL;
    EVENT_ALLOC(&test_event_no_free, 0, &head);
    EVENT_ALLOC(&test_event_no_free, 0, &tail);
    head->next_event = tail;

    uint32_t used_before, total, used_after;
    mem_get_pool_usage(0, &used_before, &total);

    EVENT_UNREF(&head);

    mem_get_pool_usage(0, &used_after, &total);
    zassert_equal(used_after, used_before - 2);
    zassert_is_null(head);
}

ZTEST(event_link, test_on_free_called_for_each_linked_event_reaching_zero)
{
    event_t* head = NULL;
    event_t* tail = NULL;
    EVENT_ALLOC(&test_event_with_free, 0, &head);
    EVENT_ALLOC(&test_event_with_free, 0, &tail);
    head->next_event = tail;

    EVENT_UNREF(&head);

    zassert_equal(free_call_count, 2);
}

ZTEST(event_link, test_chain_length_is_capped)
{
    // Link one more event than the internal cap allows
    event_t* nodes[LINKED_EVENT_CHAIN_LEN_MAX + 1] = { 0 };
    for (int i = 0; i < (int)ARRAY_SIZE(nodes); i++)
    {
        zassert_equal(EVENT_ALLOC(&test_event_no_free, 0, &nodes[i]), SUCCESS);
    }
    for (int i = 0; i < (int)ARRAY_SIZE(nodes) - 1; i++)
    {
        nodes[i]->next_event = nodes[i + 1];
    }

    event_t* head = nodes[0];
    event_t* orphan = nodes[LINKED_EVENT_CHAIN_LEN_MAX];

    EVENT_UNREF(&head);

    // The cap was reached before the last node in the chain, so it's still alive
    zassert_equal(mem_get_ref_count(orphan), 1);

    EVENT_UNREF(&orphan);
}

// ---------------------------------------------------------------------------
// Suite: event_init
// ---------------------------------------------------------------------------

ZTEST_SUITE(event_init, NULL, NULL, NULL, NULL, NULL);

ZTEST(event_init, test_init_sets_fields)
{
    uint8_t backing[sizeof(event_t) + sizeof(struct test_payload)];
    event_t* event = (event_t*)backing;
    event->next_event = (event_t*)0xDEAD;

    event_init(event, &test_event_no_free, sizeof(struct test_payload));

    zassert_is_null(event->next_event);
    zassert_equal(event->type, &test_event_no_free);
    zassert_equal(event->data.len, sizeof(struct test_payload));
}
