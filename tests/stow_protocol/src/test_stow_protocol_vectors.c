/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stow_vectors.h>

#include "test_common.h"

#define SESSION_ID 121
#define CLIENT_SIDE 0

static void before_each(void* fixture)
{
    (void)fixture;
    test_reset();
}

static void after_each(void* fixture)
{
    (void)fixture;
    test_endpoint_close(SESSION_ID);
    test_sync();
    test_drain_captures();
}

static void* setup(void)
{
    zassert_str_equal(stow_sides[CLIENT_SIDE], "client");
    return NULL;
}

ZTEST_SUITE(stow_protocol_vectors, NULL, setup, before_each, after_each, NULL);

static stow_role_t role_mask(const struct protocol_vector* vector)
{
    stow_role_t roles = 0;
    for (const char* const* name = vector->roles; *name != NULL; name++)
    {
        size_t bit = 0;
        while (bit < STOW_ROLE_COUNT && strcmp(*name, g_stow_role_names[bit]) != 0)
        {
            bit++;
        }
        zassert_true(bit < STOW_ROLE_COUNT, "%s: unknown role %s", vector->name, *name);
        roles |= (stow_role_t)BIT(bit);
    }
    return roles;
}

/* Plays a vector by the rules in tools/protocol_spec/README.md */
static void play(const struct protocol_vector* vector)
{
    stow_init();
    test_open_session(SESSION_ID, role_mask(vector));
    for (size_t i = 0; i < vector->step_count; i++)
    {
        const struct protocol_step* step = &vector->steps[i];
        if (step->side == CLIENT_SIDE)
        {
            zassert_equal(test_submit_rx(SESSION_ID, step->data, step->len), 0, "%s.steps[%zu]", vector->name, i);
            continue;
        }
        struct test_capture cap = test_await_tx(K_SECONDS(1));
        zassert_not_null(cap.buf, "%s.steps[%zu]: nothing sent, expected %s", vector->name, i, step->edn);
        bool same = cap.buf->len == step->len && memcmp(cap.buf->data, step->data, step->len) == 0;
        if (!same)
        {
            protocol_vector_print_hex("sent", cap.buf->data, cap.buf->len);
        }
        net_buf_unref(cap.buf);
        zassert_true(same, "%s.steps[%zu]: expected %s", vector->name, i, step->edn);
    }

    test_sync();
    struct test_capture extra = test_await_tx(K_NO_WAIT);
    bool sent_more = extra.buf != NULL;
    if (sent_more)
    {
        protocol_vector_print_hex("sent", extra.buf->data, extra.buf->len);
        net_buf_unref(extra.buf);
    }
    zassert_false(sent_more, "%s: sent more after the last step", vector->name);

    test_endpoint_close(SESSION_ID);
    test_sync();
}

#define PLAY(name) \
    ZTEST(stow_protocol_vectors, test_##name) { play(&stow_vector_##name); }

STOW_EXCHANGES(PLAY)
STOW_MALFORMED(PLAY)
