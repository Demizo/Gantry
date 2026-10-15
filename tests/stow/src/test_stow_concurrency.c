/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gantry/error.h>
#include <zephyr/kernel.h>

#include "generated_struct_NestedStruct.h"
#include "generated_struct_TestStruct.h"
#include "test_common.h"

#define WRITER_STACK_SIZE 1024
#define WRITER_PRIORITY 5

K_THREAD_STACK_DEFINE(writer_a_stack, WRITER_STACK_SIZE);
K_THREAD_STACK_DEFINE(writer_b_stack, WRITER_STACK_SIZE);
static struct k_thread writer_a_data;
static struct k_thread writer_b_data;

K_SEM_DEFINE(writer_start, 0, 2);
K_SEM_DEFINE(releaser_start, 0, 1);

#define RELEASE_ROUNDS 200

struct tofu_writer_args
{
    const char* value;
    int ret;
};

static void tofu_writer_thread(void* arg1, void* arg2, void* arg3)
{
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    struct tofu_writer_args* args = (struct tofu_writer_args*)arg1;

    // Wait so both writer threads attempt stow_set() on the same TOFU item as close together as
    // possible, genuinely contending rather than running sequentially.
    k_sem_take(&writer_start, K_FOREVER);

    data_value_t value = { .type = STOW_ITEM_TYPE_STRING, .data.string_value = (char*)args->value };
    args->ret = stow_set(STOW_ID_TEST_TOFU, value);
}

ZTEST_SUITE(stow_concurrency, NULL, NULL, reset_stow, NULL, NULL);

ZTEST(stow_concurrency, test_concurrent_tofu_writers_exactly_one_succeeds)
{
    struct tofu_writer_args args_a = { .value = "writer_a", .ret = -1 };
    struct tofu_writer_args args_b = { .value = "writer_b", .ret = -1 };

    k_tid_t tid_a = k_thread_create(
        &writer_a_data, writer_a_stack, WRITER_STACK_SIZE, tofu_writer_thread, &args_a, NULL, NULL, WRITER_PRIORITY, 0,
        K_NO_WAIT);
    k_tid_t tid_b = k_thread_create(
        &writer_b_data, writer_b_stack, WRITER_STACK_SIZE, tofu_writer_thread, &args_b, NULL, NULL, WRITER_PRIORITY, 0,
        K_NO_WAIT);

    k_sem_give(&writer_start);
    k_sem_give(&writer_start);

    k_thread_join(tid_a, K_FOREVER);
    k_thread_join(tid_b, K_FOREVER);

    int success_count = (args_a.ret == SUCCESS) + (args_b.ret == SUCCESS);
    int denied_count = (args_a.ret == -EALREADY) + (args_b.ret == -EALREADY);

    zassert_equal(success_count, 1, "Expected exactly one writer to succeed");
    zassert_equal(denied_count, 1, "Expected exactly one writer to be denied with -EALREADY");

    // The stored value must match whichever writer actually won, not a mix of both.
    data_value_t got = { 0 };
    stow_get(STOW_ID_TEST_TOFU, &got);
    const char* winner = (args_a.ret == SUCCESS) ? args_a.value : args_b.value;
    zassert_str_equal(got.data.string_value, winner);
    stow_release(STOW_ID_TEST_TOFU, &got);
}

static void releaser_thread(void* arg1, void* arg2, void* arg3)
{
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    data_value_t* held = arg1;
    k_sem_take(&releaser_start, K_FOREVER);
    stow_release(STOW_ID_TEST_NESTED_STRUCT_ITEM, held);
}

static uint32_t blocks_in_use(void)
{
    uint32_t total_used = 0;
    for (uint8_t pool = 0; pool < CONFIG_MEM_POOL_COUNT; pool++)
    {
        uint32_t used = 0;
        uint32_t total = 0;
        (void)mem_get_pool_usage(pool, &used, &total);
        total_used += used;
    }
    return total_used;
}

ZTEST(stow_concurrency, test_two_holders_releasing_a_nested_struct_at_once_free_every_block)
{
    uint8_t buf_storage[sizeof(buffer_t) + 1];
    buffer_t* buf = (buffer_t*)buf_storage;
    buf->len = 1;
    buf->buf[0] = 0x5A;
    TestStruct_t inner = { .enum_field = Color_BLUE, .string_field = "hi", .buf_field = buf };
    NestedStruct_t outer = { .inner = &inner };
    data_value_t next = { .type = STOW_ITEM_TYPE_STRUCT, .data.raw_value = &outer };

    uint32_t blocks = blocks_in_use();
    for (int round = 0; round < RELEASE_ROUNDS; round++)
    {
        data_value_t first;
        data_value_t second;
        zassert_equal(stow_get(STOW_ID_TEST_NESTED_STRUCT_ITEM, &first), 0);
        zassert_equal(stow_get(STOW_ID_TEST_NESTED_STRUCT_ITEM, &second), 0);
        inner.int_field = round;
        zassert_equal(stow_set(STOW_ID_TEST_NESTED_STRUCT_ITEM, next), 0);

        k_tid_t tid = k_thread_create(
            &writer_a_data, writer_a_stack, WRITER_STACK_SIZE, releaser_thread, &second, NULL, NULL, WRITER_PRIORITY, 0,
            K_NO_WAIT);
        PASS_OWNERSHIP(second);
        k_sem_give(&releaser_start);
        stow_release(STOW_ID_TEST_NESTED_STRUCT_ITEM, &first);
        k_thread_join(tid, K_FOREVER);
    }
    zassert_equal(blocks_in_use(), blocks, "nested blocks leaked");
}
