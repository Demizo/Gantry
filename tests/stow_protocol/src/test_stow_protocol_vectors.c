/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <generated_vectors.h>

#include "test_common.h"

#define SESSION_ID 121

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

ZTEST_SUITE(stow_protocol_vectors, NULL, NULL, before_each, after_each, NULL);

ZTEST(stow_protocol_vectors, test_server_answers_every_vector)
{
    for (size_t i = 0; i < ARRAY_SIZE(test_vectors); i++)
    {
        const struct test_vector* vector = &test_vectors[i];
        stow_init();
        test_open_session(SESSION_ID, vector->roles);
        for (size_t step = 0; step < vector->setup_count; step++)
        {
            zassert_equal(
                test_submit_rx(SESSION_ID, vector->setup[step].request, vector->setup[step].request_len), 0, "%s",
                vector->name);
            struct test_capture setup_cap = test_await_tx(K_SECONDS(1));
            zassert_not_null(setup_cap.buf, "%s: no setup response", vector->name);
            uint32_t cmd = 0;
            zcbor_state_t dec[4];
            zassert_true(test_decode_response_header(setup_cap.buf, &cmd, NULL, dec, ARRAY_SIZE(dec)));
            net_buf_unref(setup_cap.buf);
            zassert_equal(cmd, STOW_MSG_OK, "%s: setup step %zu failed", vector->name, step);
        }
        zassert_equal(test_submit_rx(SESSION_ID, vector->request, vector->request_len), 0, "%s", vector->name);

        struct test_capture cap = test_await_tx(K_SECONDS(1));
        zassert_not_null(cap.buf, "%s: no response", vector->name);
        zassert_equal(cap.buf->len, vector->response_len, "%s: %s", vector->name, vector->meaning);
        zassert_mem_equal(
            cap.buf->data, vector->response, vector->response_len, "%s: %s", vector->name, vector->meaning);
        net_buf_unref(cap.buf);

        test_endpoint_close(SESSION_ID);
        test_sync();
    }
}
