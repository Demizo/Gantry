/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief A test console: drives the link, and observes and drives the peer's Stow and the local Stow
 *
 * @details Every result and client event is printed as one line starting with `@`, in the order the client reports
 * them. A request to the peer prints `@sent <n>`, then `@done <n> <err> [<item>=<value>]` once it completes. Items
 * are named on the command line, so they are looked up in the generated tables by name.
 *
 * @author Demizo (demizodemazo@gmail.com)
 */

#include <gantry/error.h>
#include <gantry/event.h>
#include <gantry/module.h>
#include <gantry/stow/stow.h>
#include <generated_stow_items.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/sys/atomic.h>

#include "peer.h"

LOG_MODULE_REGISTER(peer_console, LOG_LEVEL_INF);

//**********************************************************
//* Local Definitions
//**********************************************************

#define QUEUE_DEPTH 16U
#define VALUE_SIZE 48U
#define STACK_SIZE 2048U
#define PRIORITY 8

/**
 * @brief A client event for the console thread to print
 */
struct console_msg
{
    event_t* event;    /**< Update or result event, owned by the message, or NULL for a hook */
    uint32_t request;  /**< Request number of a result */
    bool disconnected; /**< Whether a hook was disconnected rather than connected */
    int err;           /**< Error of a connect */
};

//**********************************************************
//* Static Function Declarations
//**********************************************************

static const struct shell* console(void);
static void format_value(char* out, enum stow_item_type type, const union stow_constraints* c, data_value_t value);
static int parse_value(enum stow_item_type type, const union stow_constraints* c, char* text, data_value_t* out);
static int peer_item_id(const char* name);
static const struct stow_item_const_metadata* local_item(const char* name);
static struct stow_client_peer* take_peer(void);
static void post(const struct console_msg* msg);
static void post_event(event_t* event, uint32_t request);
static void print_peer_value(const char* prefix, const struct stow_client_value* value);
static void on_connected(struct stow_client_peer* peer, int err);
static void on_disconnected(struct stow_client_peer* peer);
static void on_update(event_t* event);
static void on_result(event_t* event, void* user_data);
static void peer_console_init(void);
static void peer_console_thread(void* arg1, void* arg2, void* arg3);
static int cmd_peer(const struct shell* sh, size_t argc, char** argv);
static int cmd_peer_connect(const struct shell* sh, size_t argc, char** argv);
static int cmd_link_up(const struct shell* sh, size_t argc, char** argv);
static int cmd_link_down(const struct shell* sh, size_t argc, char** argv);
static int cmd_local_get(const struct shell* sh, size_t argc, char** argv);
static int cmd_local_set(const struct shell* sh, size_t argc, char** argv);

//**********************************************************
//* Static Variable Definitions
//**********************************************************

K_MSGQ_DEFINE(console_queue, sizeof(struct console_msg), QUEUE_DEPTH, sizeof(void*));
K_MUTEX_DEFINE(peer_lock);

static struct stow_client_peer* connected_peer;
static atomic_t last_request;

STOW_CLIENT_CB_DEFINE(peer_console_hooks) = {
    .connected = on_connected,
    .disconnected = on_disconnected,
    .update = on_update,
};

GANTRY_MODULE_DEFINE(peer_console, peer_console_init, peer_console_thread, STACK_SIZE, PRIORITY);

//**********************************************************
//* Static Function Definitions
//**********************************************************

static const struct shell* console(void) { return shell_backend_uart_get_ptr(); }

/**
 * @brief Format an int, enum or string value of up to @ref VALUE_SIZE characters
 *
 * @param out Output buffer of @ref VALUE_SIZE
 * @param type Type of the item
 * @param c Constraints of the item, for enum names
 * @param value The value
 */
static void format_value(char* out, enum stow_item_type type, const union stow_constraints* c, data_value_t value)
{
    snprintf(out, VALUE_SIZE, "%d", value.data.int_value);
    if (type == STOW_ITEM_TYPE_STRING)
    {
        snprintf(out, VALUE_SIZE, "%s", value.data.string_value);
    }
    for (uint16_t i = 0; type == STOW_ITEM_TYPE_ENUM && i < c->enum_constraints.value_count; i++)
    {
        if (c->enum_constraints.values[i].value == value.data.int_value)
        {
            snprintf(out, VALUE_SIZE, "%s", c->enum_constraints.values[i].name);
        }
    }
}

/**
 * @brief Parse an int, enum or string value; a string points into @p text
 *
 * @param type Type of the item
 * @param c Constraints of the item, for enum names
 * @param text The text
 * @param out Parsed value
 *
 * @return SUCCESS, or -EINVAL for text that is not a value of @p type
 */
static int parse_value(enum stow_item_type type, const union stow_constraints* c, char* text, data_value_t* out)
{
    out->type = type;
    out->data.string_value = text;
    if (type == STOW_ITEM_TYPE_INT)
    {
        char* end = NULL;
        out->data.int_value = (int)strtol(text, &end, 0);
        return (end != text && *end == '\0') ? SUCCESS : -EINVAL;
    }
    for (uint16_t i = 0; type == STOW_ITEM_TYPE_ENUM && i < c->enum_constraints.value_count; i++)
    {
        if (strcmp(c->enum_constraints.values[i].name, text) == 0)
        {
            out->data.int_value = c->enum_constraints.values[i].value;
            return SUCCESS;
        }
    }
    return (type == STOW_ITEM_TYPE_STRING) ? SUCCESS : -EINVAL;
}

/**
 * @brief Look up a peer item by name
 *
 * @param name Item name
 *
 * @return the generated item ID, or -ENOENT
 */
static int peer_item_id(const char* name)
{
    for (uint16_t id = 0; id < peer_client.schema->item_count; id++)
    {
        if (strcmp(peer_client.schema->items[id].name, name) == 0)
        {
            return id;
        }
    }
    return -ENOENT;
}

/**
 * @brief Look up a local item by name
 *
 * @param name Item name
 *
 * @return the item's metadata, or NULL
 */
static const struct stow_item_const_metadata* local_item(const char* name)
{
    for (size_t id = 0; id < STOW_ID_COUNT; id++)
    {
        if (strcmp(g_stow_const_metadata[id].name, name) == 0)
        {
            return &g_stow_const_metadata[id];
        }
    }
    return NULL;
}

/**
 * @brief Take a reference to the connected peer
 *
 * @return the peer, or NULL when none is connected
 *
 * @acquires stow_client_peer return
 */
static struct stow_client_peer* take_peer(void)
{
    struct stow_client_peer* peer = NULL;
    k_mutex_lock(&peer_lock, K_FOREVER);
    if (connected_peer != NULL)
    {
        peer = stow_client_peer_ref(connected_peer);
    }
    k_mutex_unlock(&peer_lock);
    PASS_OWNERSHIP(peer);
    return peer;
}

static void post(const struct console_msg* msg)
{
    if (k_msgq_put(&console_queue, msg, K_NO_WAIT) != 0)
    {
        LOG_WRN("Console queue full, event dropped");
    }
}

/**
 * @brief Queue a client event for the console thread, keeping a reference
 *
 * @param event Update or result event
 * @param request Request number of a result
 */
static void post_event(event_t* event, uint32_t request)
{
    EVENT_REF(event);
    const struct console_msg msg = { .event = event, .request = request };
    if (k_msgq_put(&console_queue, &msg, K_NO_WAIT) != 0)
    {
        LOG_WRN("Console queue full, event dropped");
        EVENT_UNREF(&event);
    }
    else
    {
        PASS_OWNERSHIP(event);
    }
}

/**
 * @brief Print a line with a peer value
 *
 * @param prefix Start of the line
 * @param value The value, or NULL for none
 */
static void print_peer_value(const char* prefix, const struct stow_client_value* value)
{
    if (value == NULL)
    {
        shell_print(console(), "%s", prefix);
        return;
    }
    const struct stow_client_item* item = &peer_client.schema->items[value->id];
    char text[VALUE_SIZE];
    format_value(text, item->type, &item->constraints, value->value);
    shell_print(console(), "%s %s=%s", prefix, item->name, text);
}

static void on_connected(struct stow_client_peer* peer, int err)
{
    if (err == SUCCESS)
    {
        k_mutex_lock(&peer_lock, K_FOREVER);
        stow_client_peer_unref(&connected_peer);
        connected_peer = stow_client_peer_ref(peer);
        PASS_OWNERSHIP(connected_peer);
        k_mutex_unlock(&peer_lock);
    }
    post(&(struct console_msg){ .err = err });
}

static void on_disconnected(struct stow_client_peer* peer)
{
    ARG_UNUSED(peer);
    k_mutex_lock(&peer_lock, K_FOREVER);
    stow_client_peer_unref(&connected_peer);
    k_mutex_unlock(&peer_lock);
    post(&(struct console_msg){ .disconnected = true });
}

static void on_update(event_t* event) { post_event(event, 0U); }

static void on_result(event_t* event, void* user_data) { post_event(event, (uint32_t)(uintptr_t)user_data); }

static void peer_console_init(void) {}

static void peer_console_thread(void* arg1, void* arg2, void* arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    while (true)
    {
        struct console_msg msg;
        k_msgq_get(&console_queue, &msg, K_FOREVER);
        event_t* event = msg.event;
        if (event == NULL && msg.disconnected)
        {
            shell_print(console(), "@disconnected");
        }
        else if (event == NULL)
        {
            shell_print(console(), "@connected %d", msg.err);
        }
        else if (event->type == &stow_client_update_event)
        {
            const struct stow_client_update_payload* update = (const void*)event->data.buf;
            print_peer_value("@update", &update->value);
            EVENT_UNREF(&event);
        }
        else
        {
            const struct stow_client_result_payload* result = (const void*)event->data.buf;
            char prefix[32];
            snprintf(prefix, sizeof(prefix), "@done %u %d", msg.request, result->err);
            print_peer_value(prefix, (result->count > 0U) ? &result->values[0] : NULL);
            EVENT_UNREF(&event);
        }
    }
}

/**
 * @brief `peer get <item>` and `peer set <item> <value>`: an asynchronous request to the peer
 *
 * @param sh The shell
 * @param argc Argument count, the command name first
 * @param argv The command name, the item name, and for Set the value
 *
 * @return 0
 */
static int cmd_peer(const struct shell* sh, size_t argc, char** argv)
{
    uint32_t request = (uint32_t)atomic_inc(&last_request) + 1U;
    void* user_data = (void*)(uintptr_t)request;
    shell_print(sh, "@sent %u", request);

    int id = peer_item_id(argv[1]);
    int ret = (id < 0) ? id : SUCCESS;
    data_value_t value = { 0 };
    if (ret == SUCCESS && argc == 3U)
    {
        const struct stow_client_item* item = &peer_client.schema->items[id];
        ret = parse_value(item->type, &item->constraints, argv[2], &value);
    }
    struct stow_client_peer* peer = take_peer();
    if (ret == SUCCESS && peer == NULL)
    {
        ret = -ENOTCONN;
    }
    if (ret == SUCCESS)
    {
        ret = (argc == 3U) ? stow_client_set_one_async(peer, (uint16_t)id, value, on_result, user_data)
                           : stow_client_get_one_async(peer, (uint16_t)id, on_result, user_data);
    }
    stow_client_peer_unref(&peer);
    if (ret != SUCCESS)
    {
        shell_print(sh, "@done %u %d", request, ret);
    }
    return 0;
}

static int cmd_peer_connect(const struct shell* sh, size_t argc, char** argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    shell_print(sh, "@connect %d", peer_link_connect());
    return 0;
}

static int cmd_link_up(const struct shell* sh, size_t argc, char** argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    shell_print(sh, "@link up %d", peer_link_up());
    return 0;
}

static int cmd_link_down(const struct shell* sh, size_t argc, char** argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    peer_link_down();
    shell_print(sh, "@link down");
    return 0;
}

static int cmd_local_get(const struct shell* sh, size_t argc, char** argv)
{
    ARG_UNUSED(argc);
    const struct stow_item_const_metadata* item = local_item(argv[1]);
    if (item == NULL)
    {
        shell_print(sh, "@local %d", -ENOENT);
        return 0;
    }
    data_value_t value;
    int ret = stow_get(item->id, &value);
    if (ret != SUCCESS)
    {
        shell_print(sh, "@local %d", ret);
        return 0;
    }
    char text[VALUE_SIZE];
    format_value(text, item->type, &item->constraints, value);
    stow_release(item->id, &value);
    shell_print(sh, "@local 0 %s=%s", item->name, text);
    return 0;
}

static int cmd_local_set(const struct shell* sh, size_t argc, char** argv)
{
    ARG_UNUSED(argc);
    const struct stow_item_const_metadata* item = local_item(argv[1]);
    data_value_t value;
    int ret = (item != NULL) ? parse_value(item->type, &item->constraints, argv[2], &value) : -ENOENT;
    if (ret == SUCCESS)
    {
        ret = stow_set(item->id, value);
    }
    shell_print(sh, "@local %d", ret);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    link_cmds, SHELL_CMD(up, NULL, "The link came up", cmd_link_up),
    SHELL_CMD(down, NULL, "The link went down", cmd_link_down), SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(link, &link_cmds, "Link to the peer", NULL);

SHELL_STATIC_SUBCMD_SET_CREATE(
    peer_cmds, SHELL_CMD(connect, NULL, "Open a client session to the peer", cmd_peer_connect),
    SHELL_CMD_ARG(get, NULL, "Get an item: <item>", cmd_peer, 2, 0),
    SHELL_CMD_ARG(set, NULL, "Set an item: <item> <value>", cmd_peer, 3, 0), SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(peer, &peer_cmds, "The peer's Stow", NULL);

SHELL_STATIC_SUBCMD_SET_CREATE(
    local_cmds, SHELL_CMD_ARG(get, NULL, "Get an item: <item>", cmd_local_get, 2, 0),
    SHELL_CMD_ARG(set, NULL, "Set an item: <item> <value>", cmd_local_set, 3, 0), SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(local, &local_cmds, "This node's Stow", NULL);
