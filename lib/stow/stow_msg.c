/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Stow protocol message codec
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#include <gantry/error.h>
#include <gantry/memory.h>
#include <gantry/stow/stow_cbor.h>
#include <gantry/stow/stow_msg.h>
#include <string.h>
#include <sys/errno.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

/**
 * @brief Logger for module
 */
LOG_MODULE_REGISTER(stow_msg, CONFIG_STOW_MSG_LOG_LEVEL);

//**********************************************************
//* Local Definitions
//**********************************************************

/**
 * @brief Largest Describe before its names: list header, code, tag, a uint32 start and a uint16 count of names
 */
#define DESCRIBE_PREFIX_MAX 11

/**
 * @brief Largest Describe Response before its descriptions: list header, code, tag, a uint32 next and chunk length
 */
#define DESCRIBE_RESPONSE_PREFIX_MAX 13

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief Which side sends a message
 */
enum direction
{
    DIRECTION_REQUEST,     /**< Client to server, answered by a response */
    DIRECTION_RESPONSE,    /**< Server to client, answering a request */
    DIRECTION_UNSOLICITED, /**< Server to client, answering nothing */
};

/**
 * @brief What a message's tag may be
 */
enum tag_rule
{
    TAG_REQUIRED, /**< 0 to STOW_TAG_MAX */
    TAG_NULL,     /**< Always null */
    TAG_NULLABLE, /**< Null, or 0 to STOW_TAG_MAX */
};

/**
 * @brief What follows a message's tag
 */
enum shape
{
    SHAPE_NONE,              /**< Nothing */
    SHAPE_IDS,               /**< One or more item IDs */
    SHAPE_PAIRS,             /**< One or more item IDs, each with a value */
    SHAPE_HELLO,             /**< Version and the Hello map */
    SHAPE_DESCRIBE,          /**< Start item and optional names */
    SHAPE_DESCRIBE_RESPONSE, /**< Next item and the chunk of descriptions */
    SHAPE_ERROR,             /**< Error code, then optional item and index */
};

/**
 * @brief One kind of message
 */
struct kind
{
    enum direction direction;      /**< Which side sends it */
    enum tag_rule tag;             /**< What its tag may be */
    enum shape shape;              /**< What follows its tag */
    bool single;                   /**< Whether it holds exactly one entry */
    enum stow_message_code answer; /**< Success response of a request */
};

/**
 * @brief An errno and the error code it travels as
 */
struct errno_code
{
    int err;                   /**< Negative errno */
    enum stow_error_code code; /**< Error code */
};

//**********************************************************
//* Static Variable Definitions
//**********************************************************

/**
 * @brief Every message kind, by code
 */
static const struct kind kinds[] = {
    [STOW_MSG_HELLO] = { DIRECTION_REQUEST, TAG_REQUIRED, SHAPE_NONE, false, STOW_MSG_HELLO_RESPONSE },
    [STOW_MSG_HELLO_RESPONSE] = { DIRECTION_RESPONSE, TAG_REQUIRED, SHAPE_HELLO },
    [STOW_MSG_DESCRIBE] = { DIRECTION_REQUEST, TAG_REQUIRED, SHAPE_DESCRIBE, false, STOW_MSG_DESCRIBE_RESPONSE },
    [STOW_MSG_DESCRIBE_RESPONSE] = { DIRECTION_RESPONSE, TAG_REQUIRED, SHAPE_DESCRIBE_RESPONSE },
    [STOW_MSG_GET] = { DIRECTION_REQUEST, TAG_REQUIRED, SHAPE_IDS, false, STOW_MSG_GET_RESPONSE },
    [STOW_MSG_GET_RESPONSE] = { DIRECTION_RESPONSE, TAG_REQUIRED, SHAPE_PAIRS },
    [STOW_MSG_SET] = { DIRECTION_REQUEST, TAG_REQUIRED, SHAPE_PAIRS, false, STOW_MSG_OK },
    [STOW_MSG_SUBSCRIBE] = { DIRECTION_REQUEST, TAG_REQUIRED, SHAPE_IDS, false, STOW_MSG_OK },
    [STOW_MSG_UNSUBSCRIBE] = { DIRECTION_REQUEST, TAG_REQUIRED, SHAPE_IDS, false, STOW_MSG_OK },
    [STOW_MSG_UPDATE] = { DIRECTION_UNSOLICITED, TAG_NULL, SHAPE_PAIRS, true },
    [STOW_MSG_OK] = { DIRECTION_RESPONSE, TAG_REQUIRED, SHAPE_NONE },
    [STOW_MSG_ERROR] = { DIRECTION_RESPONSE, TAG_NULLABLE, SHAPE_ERROR },
};

BUILD_ASSERT(ARRAY_SIZE(kinds) == STOW_MSG_COUNT, "Every message code needs a kind");

/**
 * @brief The errnos that travel as their own error code; any other is STOW_ERR_UNKNOWN
 */
static const struct errno_code errno_codes[] = {
    { -EBADMSG, STOW_ERR_MALFORMED_MSG }, { -EPROTO, STOW_ERR_UNKNOWN_MSG },       { -ENOENT, STOW_ERR_INVALID_ITEM },
    { -ENOMEM, STOW_ERR_OUT_OF_MEMORY },  { -EACCES, STOW_ERR_PERMISSION_DENIED }, { -EINVAL, STOW_ERR_INVALID_VALUE },
    { -EALREADY, STOW_ERR_ALREADY_SET },  { -E2BIG, STOW_ERR_TOO_LARGE },
};

//**********************************************************
//* Static Function Declarations
//**********************************************************

static bool is_filled(const struct stow_msg* msg);
static size_t filled_gap(const struct stow_msg_writer* writer);
static uint32_t error_code(int err);
static int error_errno(uint32_t code);

static int read_tag(zcbor_state_t* decoder, enum tag_rule rule, uint8_t* tag_out);
static int decode_entry(
    zcbor_state_t* decoder, const struct stow_msg_items* items, bool pairs, struct stow_msg_entry* entry);
static int decode_description(zcbor_state_t* decoder, struct stow_msg_description* description);
static int read_entries(
    zcbor_state_t* decoder, const struct stow_msg_items* items, const struct kind* kind, struct stow_msg* msg,
    uint32_t* item_out);
static int read_hello(zcbor_state_t* decoder, struct stow_msg_hello* hello);
static int read_names(zcbor_state_t* decoder, struct stow_msg* msg, const uint8_t** names_out);
static int read_describe_response(zcbor_state_t* decoder, struct stow_msg* msg, struct zcbor_string* chunk_out);
static int read_error(zcbor_state_t* decoder, struct stow_msg_error* error);
static int count_descriptions(zcbor_state_t* decoder, const struct zcbor_string* chunk, uint16_t* count_out);

static bool put_start(zcbor_state_t* encoder, uint32_t elements, enum stow_message_code code, uint8_t tag);
static bool put_optional(zcbor_state_t* encoder, uint32_t value);
static bool put_hello(zcbor_state_t* encoder, uint8_t tag, const struct stow_msg_hello* hello, size_t budget);
static bool put_error(zcbor_state_t* encoder, uint8_t tag, const struct stow_msg_error* error);
static bool put_name(zcbor_state_t* encoder, const void* name);
static bool put_item(zcbor_state_t* encoder, const void* element);
static int put_filled(
    struct stow_msg_writer* writer, bool (*put)(zcbor_state_t* encoder, const void* element), const void* element);

//**********************************************************
//* Static Function Definitions
//**********************************************************

/**
 * @brief Whether a message's list is filled with whole elements until one does not fit
 */
static bool is_filled(const struct stow_msg* msg)
{
    return msg->code == STOW_MSG_DESCRIBE_RESPONSE || (msg->code == STOW_MSG_DESCRIBE && msg->describe.named);
}

/**
 * @brief Where a filled list's elements start: after room for the largest prefix, which is written in front at finish
 */
static size_t filled_gap(const struct stow_msg_writer* writer)
{
    return MIN(
        (writer->msg.code == STOW_MSG_DESCRIBE) ? DESCRIBE_PREFIX_MAX : DESCRIBE_RESPONSE_PREFIX_MAX, writer->budget);
}

static uint32_t error_code(int err)
{
    for (size_t i = 0; i < ARRAY_SIZE(errno_codes); i++)
    {
        if (errno_codes[i].err == err)
        {
            return errno_codes[i].code;
        }
    }
    return STOW_ERR_UNKNOWN;
}

static int error_errno(uint32_t code)
{
    for (size_t i = 0; i < ARRAY_SIZE(errno_codes); i++)
    {
        if (errno_codes[i].code == code)
        {
            return errno_codes[i].err;
        }
    }
    return -EIO;
}

static int read_tag(zcbor_state_t* decoder, enum tag_rule rule, uint8_t* tag_out)
{
    uint32_t tag = 0;
    if (zcbor_nil_expect(decoder, NULL))
    {
        return (rule == TAG_REQUIRED) ? -EBADMSG : SUCCESS;
    }
    if (rule == TAG_NULL || !zcbor_uint32_decode(decoder, &tag) || tag > STOW_TAG_MAX)
    {
        return -EBADMSG;
    }
    *tag_out = (uint8_t)tag;
    return SUCCESS;
}

/**
 * @brief Decode an entry, skipping the value of an item the resolver does not know
 *
 * @return SUCCESS, -ENOENT for an unknown item, -EINVAL for a value that breaks its constraints, -EBADMSG or -ENOMEM
 * @acquires memory entry on-success
 */
static int decode_entry(
    zcbor_state_t* decoder, const struct stow_msg_items* items, bool pairs, struct stow_msg_entry* entry)
{
    *entry = (struct stow_msg_entry){ .id = STOW_MSG_ABSENT };
    if (!zcbor_uint32_decode(decoder, &entry->id))
    {
        return -EBADMSG;
    }
    int ret = items->resolve(items, entry);
    if (!pairs)
    {
        entry->codec = NULL;
        return ret;
    }
    if (ret != SUCCESS)
    {
        return zcbor_any_skip(decoder, NULL) ? -ENOENT : -EBADMSG;
    }

    data_value_t value = { 0 };
    ret = stow_item_decode(entry->codec, decoder, &value);
    if (ret != SUCCESS)
    {
        return (ret == -ENOMEM || ret == -EINVAL) ? ret : -EBADMSG;
    }
    entry->value = value;
    PASS_OWNERSHIP(value);
    return SUCCESS;
}

static int decode_description(zcbor_state_t* decoder, struct stow_msg_description* description)
{
    enum
    {
        SEEN_ID = BIT(0),
        SEEN_NAME = BIT(1),
        SEEN_TYPE = BIT(2),
        SEEN_CONSTRAINTS = BIT(3),
        SEEN_ALL = SEEN_ID | SEEN_NAME | SEEN_TYPE | SEEN_CONSTRAINTS,
    };
    uint32_t seen = 0;

    if (!zcbor_map_start_decode(decoder))
    {
        return -EBADMSG;
    }
    while (!zcbor_array_at_end(decoder))
    {
        uint32_t key = 0;
        if (!zcbor_uint32_decode(decoder, &key))
        {
            return -EBADMSG;
        }

        bool ok = true;
        const uint8_t* start = decoder->payload;
        switch (key)
        {
            case STOW_DESCRIBE_ID:
                ok = zcbor_uint32_decode(decoder, &description->id);
                seen |= SEEN_ID;
                break;
            case STOW_DESCRIBE_NAME:
                ok = zcbor_tstr_decode(decoder, &description->name);
                seen |= SEEN_NAME;
                break;
            case STOW_DESCRIBE_TYPE:
                ok = zcbor_uint32_decode(decoder, &description->type);
                seen |= SEEN_TYPE;
                break;
            case STOW_DESCRIBE_CONSTRAINTS:
                ok = zcbor_any_skip(decoder, NULL);
                description->constraints.value = start;
                description->constraints.len = (size_t)(decoder->payload - start);
                seen |= SEEN_CONSTRAINTS;
                break;
            default:
                ok = zcbor_any_skip(decoder, NULL);
                break;
        }
        if (!ok)
        {
            return -EBADMSG;
        }
    }
    return (zcbor_map_end_decode(decoder) && seen == SEEN_ALL) ? SUCCESS : -EBADMSG;
}

/**
 * @brief Check every entry of a message without keeping any, setting @p item_out to the item that failed
 *
 * @return SUCCESS, -EBADMSG, -ENOMEM, or -EINVAL for the first value that breaks its constraints in a well-formed list
 */
static int read_entries(
    zcbor_state_t* decoder, const struct stow_msg_items* items, const struct kind* kind, struct stow_msg* msg,
    uint32_t* item_out)
{
    int invalid = SUCCESS;
    while (!zcbor_array_at_end(decoder))
    {
        if (msg->count == UINT16_MAX)
        {
            *item_out = STOW_MSG_ABSENT;
            return -EBADMSG;
        }
        zcbor_state_t saved[STOW_MSG_READER_STATES];
        memcpy(saved, decoder, sizeof(saved));
        struct stow_msg_entry entry;
        int ret = decode_entry(decoder, items, kind->shape == SHAPE_PAIRS, &entry);
        if (ret == SUCCESS)
        {
            stow_msg_entry_release(&entry);
        }
        else if (ret == -EINVAL)
        {
            /* Constraints are a meaning check, so the rest of the message is checked first */
            memcpy(decoder, saved, sizeof(saved));
            uint32_t id = 0;
            if (!zcbor_uint32_decode(decoder, &id) || !zcbor_any_skip(decoder, NULL))
            {
                *item_out = STOW_MSG_ABSENT;
                return -EBADMSG;
            }
            if (invalid == SUCCESS)
            {
                invalid = ret;
                *item_out = entry.id;
            }
        }
        else if (ret != -ENOENT)
        {
            *item_out = entry.id;
            return ret;
        }
        msg->count++;
    }
    if (msg->count == 0U || (kind->single && msg->count != 1U))
    {
        *item_out = STOW_MSG_ABSENT;
        return -EBADMSG;
    }
    return invalid;
}

static int read_hello(zcbor_state_t* decoder, struct stow_msg_hello* hello)
{
    uint32_t version = 0;
    if (!zcbor_uint32_decode(decoder, &version))
    {
        return -EBADMSG;
    }
    if (version != STOW_PROTOCOL_VERSION)
    {
        return -EPROTONOSUPPORT;
    }
    if (!zcbor_map_start_decode(decoder))
    {
        return -EBADMSG;
    }

    *hello = (struct stow_msg_hello){ 0 };
    while (!zcbor_array_at_end(decoder))
    {
        uint32_t key = 0;
        uint32_t roles = 0;
        if (!zcbor_uint32_decode(decoder, &key))
        {
            return -EBADMSG;
        }
        bool ok = true;
        switch (key)
        {
            case STOW_HELLO_MAX_MESSAGE:
                ok = zcbor_uint32_decode(decoder, &hello->max_message);
                break;
            case STOW_HELLO_STOW_HASH:
                ok = zcbor_tstr_decode(decoder, &hello->stow_hash);
                break;
            case STOW_HELLO_ROLES:
                ok = zcbor_uint32_decode(decoder, &roles) && roles <= UINT16_MAX;
                hello->roles = (stow_role_t)roles;
                break;
            default:
                ok = zcbor_any_skip(decoder, NULL);
                break;
        }
        if (!ok)
        {
            return -EBADMSG;
        }
    }
    return (zcbor_map_end_decode(decoder) && hello->max_message > 0U) ? SUCCESS : -EBADMSG;
}

/**
 * @brief Check a Describe's start and names, pointing @p names_out at the first name
 */
static int read_names(zcbor_state_t* decoder, struct stow_msg* msg, const uint8_t** names_out)
{
    if (!zcbor_uint32_decode(decoder, &msg->describe.start))
    {
        return -EBADMSG;
    }
    if (zcbor_array_at_end(decoder))
    {
        return SUCCESS;
    }

    msg->describe.named = true;
    if (!zcbor_list_start_decode(decoder))
    {
        return -EBADMSG;
    }
    *names_out = decoder->payload;
    while (!zcbor_array_at_end(decoder))
    {
        struct zcbor_string name;
        if (msg->count == UINT16_MAX || !zcbor_tstr_decode(decoder, &name))
        {
            return -EBADMSG;
        }
        msg->count++;
    }
    return zcbor_list_end_decode(decoder) ? SUCCESS : -EBADMSG;
}

static int read_describe_response(zcbor_state_t* decoder, struct stow_msg* msg, struct zcbor_string* chunk_out)
{
    msg->describe_response.next = STOW_MSG_ABSENT;
    if (!zcbor_nil_expect(decoder, NULL) && !zcbor_uint32_decode(decoder, &msg->describe_response.next))
    {
        return -EBADMSG;
    }
    return zcbor_bstr_decode(decoder, chunk_out) ? SUCCESS : -EBADMSG;
}

static int read_error(zcbor_state_t* decoder, struct stow_msg_error* error)
{
    uint32_t code = 0;
    *error = (struct stow_msg_error){ .item = STOW_MSG_ABSENT, .index = STOW_MSG_ABSENT };
    if (!zcbor_uint32_decode(decoder, &code))
    {
        return -EBADMSG;
    }
    error->err = error_errno(code);
    if (!zcbor_array_at_end(decoder) && !zcbor_nil_expect(decoder, NULL) && !zcbor_uint32_decode(decoder, &error->item))
    {
        return -EBADMSG;
    }
    if (!zcbor_array_at_end(decoder) && !zcbor_uint32_decode(decoder, &error->index))
    {
        return -EBADMSG;
    }
    return SUCCESS;
}

/**
 * @brief Check every description of a chunk, leaving @p decoder over it
 */
static int count_descriptions(zcbor_state_t* decoder, const struct zcbor_string* chunk, uint16_t* count_out)
{
    zcbor_new_decode_state(decoder, STOW_MSG_READER_STATES, chunk->value, chunk->len, ZCBOR_MAX_ELEM_COUNT, NULL, 0);
    while (decoder->payload < decoder->payload_end)
    {
        struct stow_msg_description description;
        if (*count_out == UINT16_MAX || decode_description(decoder, &description) != SUCCESS)
        {
            return -EBADMSG;
        }
        (*count_out)++;
    }
    return SUCCESS;
}

static bool put_start(zcbor_state_t* encoder, uint32_t elements, enum stow_message_code code, uint8_t tag)
{
    return stow_cbor_list_start(encoder, elements) && zcbor_uint32_put(encoder, code) &&
           ((tag == STOW_TAG_NONE) ? zcbor_nil_put(encoder, NULL) : zcbor_uint32_put(encoder, tag));
}

static bool put_optional(zcbor_state_t* encoder, uint32_t value)
{
    return (value == STOW_MSG_ABSENT) ? zcbor_nil_put(encoder, NULL) : zcbor_uint32_put(encoder, value);
}

static bool put_hello(zcbor_state_t* encoder, uint8_t tag, const struct stow_msg_hello* hello, size_t budget)
{
    if (!put_start(encoder, 4, STOW_MSG_HELLO_RESPONSE, tag) || !zcbor_uint32_put(encoder, STOW_PROTOCOL_VERSION) ||
        !stow_cbor_map_start(encoder, 4) || !zcbor_uint32_put(encoder, STOW_HELLO_MAX_MESSAGE) ||
        !zcbor_uint32_put(encoder, (uint32_t)budget) || !zcbor_uint32_put(encoder, STOW_HELLO_STOW_HASH) ||
        !zcbor_tstr_encode(encoder, &hello->stow_hash) || !zcbor_uint32_put(encoder, STOW_HELLO_ROLES) ||
        !zcbor_uint32_put(encoder, hello->roles) || !zcbor_uint32_put(encoder, STOW_HELLO_ROLE_NAMES) ||
        !stow_cbor_list_start(encoder, hello->role_count))
    {
        return false;
    }
    for (uint8_t bit = 0; bit < hello->role_count; bit++)
    {
        if (!zcbor_tstr_put_term(encoder, hello->role_names[bit], SIZE_MAX))
        {
            return false;
        }
    }
    return true;
}

static bool put_error(zcbor_state_t* encoder, uint8_t tag, const struct stow_msg_error* error)
{
    bool has_index = (error->index != STOW_MSG_ABSENT);
    bool has_item = has_index || (error->item != STOW_MSG_ABSENT);
    return put_start(encoder, 3U + (has_item ? 1U : 0U) + (has_index ? 1U : 0U), STOW_MSG_ERROR, tag) &&
           zcbor_uint32_put(encoder, error_code(error->err)) && (!has_item || put_optional(encoder, error->item)) &&
           (!has_index || zcbor_uint32_put(encoder, error->index));
}

static bool put_name(zcbor_state_t* encoder, const void* name) { return zcbor_tstr_put_term(encoder, name, SIZE_MAX); }

static bool put_item(zcbor_state_t* encoder, const void* element)
{
    const struct stow_item_const_metadata* item = element;
    data_value_t default_value = { .type = item->type, .data = item->default_value };
    if (!stow_cbor_map_start(encoder, 9) || !zcbor_uint32_put(encoder, STOW_DESCRIBE_ID) ||
        !zcbor_uint32_put(encoder, item->id) || !zcbor_uint32_put(encoder, STOW_DESCRIBE_NAME) ||
        !zcbor_tstr_put_term(encoder, item->name, SIZE_MAX) || !zcbor_uint32_put(encoder, STOW_DESCRIBE_CATEGORIES) ||
        !stow_cbor_list_start(encoder, item->category_count))
    {
        return false;
    }
    for (uint8_t i = 0; i < item->category_count; i++)
    {
        if (!zcbor_tstr_put_term(encoder, item->categories[i], SIZE_MAX))
        {
            return false;
        }
    }
    return zcbor_uint32_put(encoder, STOW_DESCRIBE_STORAGE) && zcbor_uint32_put(encoder, item->storage_type) &&
           zcbor_uint32_put(encoder, STOW_DESCRIBE_READ_PERM) &&
           zcbor_uint32_put(encoder, item->permissions.read_permissions) &&
           zcbor_uint32_put(encoder, STOW_DESCRIBE_WRITE_PERM) &&
           zcbor_uint32_put(encoder, item->permissions.write_permissions) &&
           zcbor_uint32_put(encoder, STOW_DESCRIBE_TYPE) && zcbor_uint32_put(encoder, item->type) &&
           zcbor_uint32_put(encoder, STOW_DESCRIBE_DEFAULT) &&
           item->interface->encode(encoder, default_value) == SUCCESS &&
           zcbor_uint32_put(encoder, STOW_DESCRIBE_CONSTRAINTS) &&
           item->interface->encode_constraints(encoder, &item->constraints) == SUCCESS;
}

/**
 * @brief Put the next element of a filled list, rolling it back when it does not fit
 *
 * @return SUCCESS, or -E2BIG when it or an earlier element was rejected
 */
static int put_filled(
    struct stow_msg_writer* writer, bool (*put)(zcbor_state_t* encoder, const void* element), const void* element)
{
    if (writer->rejected)
    {
        return -E2BIG;
    }
    zcbor_state_t saved[STOW_MSG_WRITER_STATES];
    memcpy(saved, writer->states, sizeof(saved));
    if (writer->msg.count == UINT16_MAX || !put(writer->states, element))
    {
        memcpy(writer->states, saved, sizeof(saved));
        writer->rejected = true;
        return -E2BIG;
    }
    writer->msg.count++;
    return SUCCESS;
}

//**********************************************************
//* Public Function Definitions
//**********************************************************

int stow_msg_peek(const uint8_t* data, size_t len, uint32_t* code_out, uint8_t* tag_out)
{
    zcbor_state_t decoder[STOW_MSG_READER_STATES];
    zcbor_new_decode_state(decoder, ARRAY_SIZE(decoder), data, len, 1, NULL, 0);
    uint32_t code = STOW_MSG_ABSENT;
    *tag_out = STOW_TAG_NONE;
    bool ok = zcbor_list_start_decode(decoder) && zcbor_uint32_decode(decoder, &code);
    *code_out = code;
    if (!ok)
    {
        return -EBADMSG;
    }
    return read_tag(decoder, TAG_NULLABLE, tag_out);
}

int stow_msg_read(
    struct stow_msg_reader* reader, const uint8_t* data, size_t len, const struct stow_msg_items* items,
    struct stow_msg* msg)
{
    zcbor_state_t* decoder = reader->states;
    zcbor_new_decode_state(decoder, ARRAY_SIZE(reader->states), data, len, 1, NULL, 0);
    reader->items = items;
    reader->left = 0;
    *msg = (struct stow_msg){ .tag = STOW_TAG_NONE };

    uint32_t code = STOW_MSG_ABSENT;
    uint32_t item = STOW_MSG_ABSENT;
    const uint8_t* list = NULL;
    struct zcbor_string chunk = { 0 };
    int ret = (zcbor_list_start_decode(decoder) && zcbor_uint32_decode(decoder, &code)) ? SUCCESS : -EBADMSG;

    /* An unknown code's tag is checked as a request's, the only kind a peer sends without knowing it */
    const struct kind* kind = (code < STOW_MSG_COUNT) ? &kinds[code] : NULL;
    uint8_t tag = STOW_TAG_NONE;
    if (ret == SUCCESS)
    {
        ret = read_tag(decoder, (kind != NULL) ? kind->tag : TAG_REQUIRED, &tag);
    }
    if (ret == SUCCESS && kind == NULL)
    {
        ret = -EPROTO;
    }

    if (ret == SUCCESS)
    {
        msg->code = (enum stow_message_code)code;
        list = decoder->payload;
        switch (kind->shape)
        {
            case SHAPE_NONE:
                break;
            case SHAPE_IDS:
            case SHAPE_PAIRS:
                ret = read_entries(decoder, items, kind, msg, &item);
                break;
            case SHAPE_HELLO:
                ret = read_hello(decoder, &msg->hello);
                break;
            case SHAPE_DESCRIBE:
                ret = read_names(decoder, msg, &list);
                break;
            case SHAPE_DESCRIBE_RESPONSE:
                ret = read_describe_response(decoder, msg, &chunk);
                break;
            case SHAPE_ERROR:
                ret = read_error(decoder, &msg->error);
                break;
        }
    }
    if ((ret == SUCCESS || ret == -EINVAL) && (!zcbor_list_end_decode(decoder) || decoder->payload != data + len))
    {
        ret = -EBADMSG;
        item = STOW_MSG_ABSENT;
    }
    if (ret == SUCCESS && kind->shape == SHAPE_DESCRIBE_RESPONSE)
    {
        ret = count_descriptions(decoder, &chunk, &msg->count);
    }

    msg->tag = tag;
    if (ret != SUCCESS)
    {
        LOG_DBG("Message %u failed to read (%d)", code, ret);
        msg->error = (struct stow_msg_error){ .err = ret, .item = item, .index = STOW_MSG_ABSENT };
        return ret;
    }

    reader->code = msg->code;
    reader->left = msg->count;
    if (kind->shape == SHAPE_DESCRIBE_RESPONSE)
    {
        zcbor_new_decode_state(
            decoder, ARRAY_SIZE(reader->states), chunk.value, chunk.len, ZCBOR_MAX_ELEM_COUNT, NULL, 0);
    }
    else if (msg->count > 0U)
    {
        zcbor_new_decode_state(
            decoder, ARRAY_SIZE(reader->states), list, (size_t)(data + len - list), ZCBOR_MAX_ELEM_COUNT, NULL, 0);
    }
    return SUCCESS;
}

int stow_msg_next_entry(struct stow_msg_reader* reader, struct stow_msg_entry* entry)
{
    enum shape shape = kinds[reader->code].shape;
    ASSERT(
        (shape == SHAPE_IDS || shape == SHAPE_PAIRS) && reader->left > 0U, "No entry left in message %u", reader->code);
    reader->left--;
    int ret = decode_entry(reader->states, reader->items, shape == SHAPE_PAIRS, entry);
    ASSERT(ret != -EBADMSG, "Message %u changed after it was read", reader->code);
    PASS_OWNERSHIP(entry);
    return ret;
}

int stow_msg_next_name(struct stow_msg_reader* reader, struct zcbor_string* name)
{
    ASSERT(reader->code == STOW_MSG_DESCRIBE && reader->left > 0U, "No name left in message %u", reader->code);
    reader->left--;
    bool ok = zcbor_tstr_decode(reader->states, name);
    ASSERT(ok, "Message %u changed after it was read", reader->code);
    return SUCCESS;
}

int stow_msg_next_description(struct stow_msg_reader* reader, struct stow_msg_description* description)
{
    ASSERT(
        reader->code == STOW_MSG_DESCRIBE_RESPONSE && reader->left > 0U, "No description left in message %u",
        reader->code);
    reader->left--;
    int ret = decode_description(reader->states, description);
    ASSERT(ret == SUCCESS, "Message %u changed after it was read", reader->code);
    return ret;
}

void stow_msg_writer_init(struct stow_msg_writer* writer, uint8_t* out, size_t budget)
{
    writer->out = out;
    writer->budget = budget;
    writer->msg = (struct stow_msg){ .code = STOW_MSG_COUNT };
    writer->err = SUCCESS;
    writer->left = 0;
    writer->rejected = false;
}

int stow_msg_write(struct stow_msg_writer* writer, const struct stow_msg* msg)
{
    ASSERT(msg->code < STOW_MSG_COUNT, "Unknown message code %u", msg->code);
    const struct kind* kind = &kinds[msg->code];
    bool tag_ok =
        (msg->tag == STOW_TAG_NONE) ? (kind->tag != TAG_REQUIRED) : (msg->tag <= STOW_TAG_MAX && kind->tag != TAG_NULL);
    ASSERT(tag_ok, "Tag %u breaks the tag rule of message %u", msg->tag, msg->code);

    writer->msg = *msg;
    writer->err = SUCCESS;
    writer->left = 0;
    writer->rejected = false;
    zcbor_state_t* encoder = writer->states;

    if (is_filled(msg))
    {
        size_t gap = filled_gap(writer);
        writer->msg.count = 0;
        if (msg->code == STOW_MSG_DESCRIBE_RESPONSE)
        {
            writer->msg.describe_response.next = STOW_MSG_ABSENT;
        }
        zcbor_new_encode_state(encoder, ARRAY_SIZE(writer->states), writer->out + gap, writer->budget - gap, 1);
        return SUCCESS;
    }

    zcbor_new_encode_state(encoder, ARRAY_SIZE(writer->states), writer->out, writer->budget, 1);
    bool ok = true;
    switch (kind->shape)
    {
        case SHAPE_NONE:
            ok = put_start(encoder, 2, msg->code, msg->tag);
            break;
        case SHAPE_IDS:
        case SHAPE_PAIRS:
            ASSERT(
                msg->count > 0U && (!kind->single || msg->count == 1U), "Message %u cannot hold %u entries", msg->code,
                msg->count);
            ok = put_start(encoder, 2U + ((kind->shape == SHAPE_PAIRS) ? 2U : 1U) * msg->count, msg->code, msg->tag);
            writer->left = msg->count;
            break;
        case SHAPE_HELLO:
            ok = put_hello(encoder, msg->tag, &msg->hello, writer->budget);
            break;
        case SHAPE_DESCRIBE:
            ok = put_start(encoder, 3, msg->code, msg->tag) && zcbor_uint32_put(encoder, msg->describe.start);
            break;
        case SHAPE_DESCRIBE_RESPONSE:
            break;
        case SHAPE_ERROR:
            ok = put_error(encoder, msg->tag, &msg->error);
            break;
    }
    writer->err = ok ? SUCCESS : -E2BIG;
    return writer->err;
}

int stow_msg_put_entry(struct stow_msg_writer* writer, const struct stow_msg_entry* entry)
{
    enum shape shape = kinds[writer->msg.code].shape;
    ASSERT(shape == SHAPE_IDS || shape == SHAPE_PAIRS, "Message %u has no entries", writer->msg.code);
    if (writer->err != SUCCESS)
    {
        return writer->err;
    }
    ASSERT(writer->left > 0U, "Message %u has more entries than its count", writer->msg.code);
    writer->left--;

    int ret = zcbor_uint32_put(writer->states, entry->id) ? SUCCESS : -E2BIG;
    if (ret == SUCCESS && shape == SHAPE_PAIRS)
    {
        ret = entry->codec->encode(writer->states, entry->value);
    }
    writer->err = (ret == -ENOMEM) ? -E2BIG : ret;
    return writer->err;
}

int stow_msg_put_name(struct stow_msg_writer* writer, const char* name)
{
    ASSERT(
        writer->msg.code == STOW_MSG_DESCRIBE && writer->msg.describe.named, "Message %u has no names",
        writer->msg.code);
    return put_filled(writer, put_name, name);
}

int stow_msg_put_description(struct stow_msg_writer* writer, const struct stow_item_const_metadata* item)
{
    ASSERT(writer->msg.code == STOW_MSG_DESCRIBE_RESPONSE, "Message %u has no descriptions", writer->msg.code);
    int ret = put_filled(writer, put_item, item);
    if (ret != SUCCESS && writer->msg.describe_response.next == STOW_MSG_ABSENT)
    {
        writer->msg.describe_response.next = item->id;
    }
    return ret;
}

int stow_msg_finish(struct stow_msg_writer* writer, size_t* len_out)
{
    ASSERT(writer->msg.code < STOW_MSG_COUNT, "No message was written");
    if (writer->err != SUCCESS)
    {
        return writer->err;
    }
    ASSERT(writer->left == 0U, "Message %u is missing %u entries", writer->msg.code, writer->left);

    if (!is_filled(&writer->msg))
    {
        *len_out = (size_t)(writer->states[0].payload - writer->out);
        return SUCCESS;
    }
    if (writer->msg.count == 0U && writer->rejected)
    {
        return -E2BIG;
    }

    const uint8_t* elements = writer->out + filled_gap(writer);
    size_t elements_len = (size_t)(writer->states[0].payload - elements);
    zcbor_state_t prefix[STOW_MSG_WRITER_STATES];
    zcbor_new_encode_state(prefix, ARRAY_SIZE(prefix), writer->out, (size_t)(elements - writer->out), 1);
    bool ok =
        put_start(prefix, 4, writer->msg.code, writer->msg.tag) &&
        ((writer->msg.code == STOW_MSG_DESCRIBE)
             ? zcbor_uint32_put(prefix, writer->msg.describe.start) && stow_cbor_list_start(prefix, writer->msg.count)
             : put_optional(prefix, writer->msg.describe_response.next) &&
                   stow_cbor_container_start(prefix, ZCBOR_MAJOR_TYPE_BSTR, (uint32_t)elements_len));
    if (!ok)
    {
        return -E2BIG;
    }

    size_t prefix_len = (size_t)(prefix[0].payload - writer->out);
    memmove(writer->out + prefix_len, elements, elements_len);
    *len_out = prefix_len + elements_len;
    return SUCCESS;
}

bool stow_msg_is_request(uint32_t code) { return code < STOW_MSG_COUNT && kinds[code].direction == DIRECTION_REQUEST; }

enum stow_message_code stow_msg_answer(enum stow_message_code request)
{
    ASSERT(stow_msg_is_request(request), "Message %u is not a request", request);
    return kinds[request].answer;
}
