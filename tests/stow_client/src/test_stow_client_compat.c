/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"

static struct test_link link;
static struct stow_client_peer* peer;

static void before_each(void* fixture)
{
    ARG_UNUSED(fixture);
    test_reset();
    peer = test_connect(&link, &test_variant_client, STOW_ROLE_GUEST);
}

static void after_each(void* fixture)
{
    ARG_UNUSED(fixture);
    test_disconnect(&link, &peer);
}

ZTEST_SUITE(stow_client_compat, NULL, NULL, before_each, after_each, NULL);

ZTEST(stow_client_compat, test_items_are_matched_by_name_type_and_constraints)
{
    zassert_equal(stow_client_availability(peer, VARIANT_ID_GONE), STOW_CLIENT_MISSING);
    zassert_equal(stow_client_availability(peer, VARIANT_ID_RATIO), STOW_CLIENT_INCOMPATIBLE, "type changed");
    zassert_equal(stow_client_availability(peer, VARIANT_ID_LABEL), STOW_CLIENT_INCOMPATIBLE, "constraints changed");
    zassert_equal(stow_client_availability(peer, VARIANT_ID_POSITION), STOW_CLIENT_INCOMPATIBLE, "struct changed");
    zassert_equal(stow_client_availability(peer, VARIANT_ID_ONCE), STOW_CLIENT_AVAILABLE);
    zassert_equal(stow_client_availability(peer, VARIANT_ID_MODE), STOW_CLIENT_AVAILABLE);
    zassert_equal(stow_client_availability(peer, VARIANT_ID_COUNTER), STOW_CLIENT_AVAILABLE);
}

ZTEST(stow_client_compat, test_unavailable_items_are_not_supported)
{
    uint32_t blocks = test_blocks_in_use();
    const uint16_t unavailable[] = { VARIANT_ID_GONE, VARIANT_ID_RATIO, VARIANT_ID_LABEL, VARIANT_ID_POSITION };
    for (size_t i = 0; i < ARRAY_SIZE(unavailable); i++)
    {
        struct stow_client_value value;
        zassert_equal(stow_client_get(peer, &unavailable[i], 1, &value), -ENOTSUP, "item %u", unavailable[i]);
    }

    const uint16_t mixed[] = { VARIANT_ID_COUNTER, VARIANT_ID_GONE };
    struct stow_client_value values[2];
    zassert_equal(stow_client_get(peer, mixed, 2, values), -ENOTSUP);
    zassert_equal(test_blocks_in_use(), blocks);
}

ZTEST(stow_client_compat, test_a_peer_with_small_messages_is_verified_in_several_describes)
{
    test_disconnect(&link, &peer);
    test_server_headroom = TEST_BUF_SIZE - 128;
    test_client_headroom = TEST_BUF_SIZE - 40;
    peer = test_connect(&link, &test_variant_client, STOW_ROLE_GUEST);

    struct stow_client_peer_info info;
    zassert_equal(stow_client_peer_info(peer, &info), 0);
    zassert_equal(info.max_message, 128);
    zassert_equal(stow_client_availability(peer, VARIANT_ID_GONE), STOW_CLIENT_MISSING);
    zassert_equal(stow_client_availability(peer, VARIANT_ID_ONCE), STOW_CLIENT_AVAILABLE);
    zassert_equal(stow_client_availability(peer, VARIANT_ID_MODE), STOW_CLIENT_AVAILABLE);
    zassert_equal(stow_client_availability(peer, VARIANT_ID_LABEL), STOW_CLIENT_INCOMPATIBLE);
    zassert_equal(stow_client_availability(peer, VARIANT_ID_COUNTER), STOW_CLIENT_AVAILABLE);
}

ZTEST(stow_client_compat, test_remapped_items_keep_working)
{
    const uint16_t ids[] = { VARIANT_ID_COUNTER, VARIANT_ID_MODE };
    const data_value_t values[] = {
        { .type = STOW_ITEM_TYPE_INT, .data.int_value = -9 },
        { .type = STOW_ITEM_TYPE_ENUM, .data.int_value = variant_Mode_AUTO },
    };
    zassert_equal(stow_client_set(peer, ids, values, 2), 0);

    data_value_t local;
    zassert_equal(stow_get(STOW_ID_COUNTER, &local), 0);
    zassert_equal(local.data.int_value, -9);
    zassert_equal(stow_get(STOW_ID_MODE, &local), 0);
    zassert_equal(local.data.int_value, Mode_AUTO);

    struct stow_client_value got[2];
    zassert_equal(stow_client_get(peer, ids, 2, got), 0);
    zassert_equal(got[0].id, VARIANT_ID_COUNTER);
    zassert_equal(got[0].value.data.int_value, -9);
    zassert_equal(got[1].value.data.int_value, variant_Mode_AUTO);
}
