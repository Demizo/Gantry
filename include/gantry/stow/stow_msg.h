/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Encoding and decoding of every Stow protocol message, for the server and the client
 *
 * @details A @ref stow_msg_items resolver maps the item IDs on the wire to this side's items and their value codecs.
 *
 * @author Demizo (demizodemazo@gmail.com)
 *
 */

#pragma once

#include <gantry/stow/types/stow_types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zcbor_common.h>
#include <zephyr/sys/util.h>

/**
 * @addtogroup stow_protocol
 * @{
 */

//**********************************************************
//* Definitions
//**********************************************************

/**
 * @brief Marks a tag that is null or could not be read
 */
#define STOW_TAG_NONE UINT8_MAX

/**
 * @brief Marks an item, index or next item that is absent from a message, or a code that could not be read
 */
#define STOW_MSG_ABSENT UINT32_MAX

/**
 * @brief zcbor states a reader holds
 *
 * @details A backup for the message's list, then one for a Hello map or a names list, or one per level of a struct
 * value, plus the two states zcbor needs. The build sets STOW_DEPTH_MAX to the deepest struct nesting it decodes.
 */
#define STOW_MSG_READER_STATES (MAX(STOW_DEPTH_MAX, 1) + 3)

/**
 * @brief zcbor states a writer holds; definite-length headers need no backups
 */
#define STOW_MSG_WRITER_STATES 2

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief One item of a Get, Set, Subscribe, Unsubscribe, Get Response or Update
 */
struct stow_msg_entry
{
    uint32_t id;                             /**< Item ID on the wire */
    uint16_t local;                          /**< This side's ID, set by the resolver */
    const struct stow_item_interface* codec; /**< Codec of the item's value, set by the resolver; NULL without one */
    data_value_t value;                      /**< Value of a Set, Get Response or Update entry */
};

/**
 * @brief What a wire item ID means on this side
 */
struct stow_msg_items
{
    /**
     * @brief Set @p entry's local ID and codec from its wire ID
     *
     * @param items The resolver
     * @param entry Entry whose @ref stow_msg_entry.id is set
     *
     * @return SUCCESS, or -ENOENT when this side has no such item
     */
    int (*resolve)(const struct stow_msg_items* items, struct stow_msg_entry* entry);
};

/**
 * @brief Body of a Hello Response
 */
struct stow_msg_hello
{
    uint32_t max_message;          /**< Read only; a written Hello Response reports the writer's budget */
    struct zcbor_string stow_hash; /**< Current StowHash value */
    stow_role_t roles;             /**< Roles the session holds */
    const char* const* role_names; /**< Write only: every role's name, indexed by its bit */
    uint8_t role_count;            /**< Write only: number of role names */
};

/**
 * @brief Body of a Describe
 */
struct stow_msg_describe
{
    uint32_t start; /**< Item to describe from */
    bool named;     /**< Whether the request names the items to describe */
};

/**
 * @brief Body of a Describe Response
 */
struct stow_msg_describe_response
{
    uint32_t next; /**< Read only: item to continue from, or @ref STOW_MSG_ABSENT; the writer derives it */
};

/**
 * @brief Body of an Error, or why a message could not be read
 */
struct stow_msg_error
{
    int err;        /**< Negative errno */
    uint32_t item;  /**< Item that failed, or @ref STOW_MSG_ABSENT */
    uint32_t index; /**< Entry that failed as it was applied, or @ref STOW_MSG_ABSENT */
};

/**
 * @brief The parts of an item description a client checks
 */
struct stow_msg_description
{
    uint32_t id;                     /**< The describing side's ID for the item */
    struct zcbor_string name;        /**< Item name */
    uint32_t type;                   /**< Item type code */
    struct zcbor_string constraints; /**< The encoded constraints */
};

/**
 * @brief Any Stow message
 */
struct stow_msg
{
    enum stow_message_code code; /**< Message code */
    uint8_t tag;                 /**< Tag, or @ref STOW_TAG_NONE */
    uint16_t count;              /**< Entries; names or descriptions when read */
    union
    {
        struct stow_msg_hello hello;                         /**< Hello Response */
        struct stow_msg_describe describe;                   /**< Describe */
        struct stow_msg_describe_response describe_response; /**< Describe Response */
        struct stow_msg_error error;                         /**< Error */
    };
};

/**
 * @brief Reads one message and then walks its list; its fields are private
 */
struct stow_msg_reader
{
    zcbor_state_t states[STOW_MSG_READER_STATES]; /**< Decoder over the list being walked */
    const struct stow_msg_items* items;           /**< Resolver of the message's item IDs */
    enum stow_message_code code;                  /**< Code of the message read */
    uint16_t left;                                /**< Elements still to walk */
};

/**
 * @brief Writes one message at a time into a byte range; its fields are private
 */
struct stow_msg_writer
{
    zcbor_state_t states[STOW_MSG_WRITER_STATES]; /**< Encoder after what is written */
    uint8_t* out;                                 /**< Start of the byte range */
    size_t budget;                                /**< Bytes the message may use */
    struct stow_msg msg;                          /**< The message being written */
    int err;                                      /**< First error since the message was started */
    uint16_t left;                                /**< Entries still to put */
    bool rejected;                                /**< Whether a filled list rejected an element */
};

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Read a message's code and tag without checking the rest
 *
 * @param data The message
 * @param len Length of @p data
 * @param[out] code_out Message code, or @ref STOW_MSG_ABSENT when it could not be read
 * @param[out] tag_out Tag, or @ref STOW_TAG_NONE when it is null or could not be read
 *
 * @return SUCCESS, or -EBADMSG when the code or tag could not be read
 */
int stow_msg_peek(const uint8_t* data, size_t len, uint32_t* code_out, uint8_t* tag_out);

/**
 * @brief Check a whole message, then make its list ready to walk
 *
 * @details Every value is decoded through @p items and released. Unknown items pass; walking reports them. On
 * failure, @p msg holds the tag if it could be read and @ref stow_msg.error the errno and the item that broke the
 * message.
 *
 * @param reader Reader, which walks the message's list next
 * @param data The message, which must outlive the walk
 * @param len Length of @p data
 * @param items Resolver of the message's item IDs
 * @param[out] msg The message's fixed fields and count
 *
 * @return SUCCESS when the message is well formed
 * @return -EBADMSG when it is malformed
 * @return -EPROTO when its code is unknown
 * @return -EPROTONOSUPPORT when it is a Hello Response of another protocol version
 * @return -EINVAL when a value breaks its item's constraints
 * @return -ENOMEM when no memory is free to check a value
 */
int stow_msg_read(
    struct stow_msg_reader* reader, const uint8_t* data, size_t len, const struct stow_msg_items* items,
    struct stow_msg* msg);

/**
 * @brief Read the next entry; call it exactly @ref stow_msg.count times
 *
 * @param reader Reader of a message with entries
 * @param[out] entry The entry; its @ref stow_msg_entry.id is set even on failure
 *
 * @return SUCCESS
 * @return -ENOENT when this side has no such item
 * @return -ENOMEM when no memory is free for the value
 * @acquires memory entry on-success
 */
int stow_msg_next_entry(struct stow_msg_reader* reader, struct stow_msg_entry* entry);

/**
 * @brief Release an entry from @ref stow_msg_next_entry; an entry without a value releases nothing
 *
 * @param entry The entry
 * @releases memory entry
 */
static inline void stow_msg_entry_release(struct stow_msg_entry* entry)
{
    data_value_t* value = &entry->value;
    if (entry->codec != NULL)
    {
        stow_item_release(entry->codec, value);
    }
}

/**
 * @brief Read the next name of a Describe; call it exactly @ref stow_msg.count times
 *
 * @param reader Reader of a Describe
 * @param[out] name The name, pointing into the message
 *
 * @return SUCCESS
 */
int stow_msg_next_name(struct stow_msg_reader* reader, struct zcbor_string* name);

/**
 * @brief Read the next description of a Describe Response; call it exactly @ref stow_msg.count times
 *
 * @param reader Reader of a Describe Response
 * @param[out] description The description, pointing into the message
 *
 * @return SUCCESS
 */
int stow_msg_next_description(struct stow_msg_reader* reader, struct stow_msg_description* description);

/**
 * @brief Start a writer on a byte range
 *
 * @param writer The writer
 * @param out Where messages are written
 * @param budget Bytes a message may use
 */
void stow_msg_writer_init(struct stow_msg_writer* writer, uint8_t* out, size_t budget);

/**
 * @brief Start a message, discarding anything written before
 *
 * @details Writes the fixed fields. A Get, Set, Subscribe, Unsubscribe, Get Response or Update then takes exactly
 * @ref stow_msg.count calls to @ref stow_msg_put_entry. A Describe that names items takes @ref stow_msg_put_name and a
 * Describe Response @ref stow_msg_put_description until one is rejected.
 *
 * @param writer The writer
 * @param msg The message
 *
 * @return SUCCESS, or -E2BIG when the fixed fields do not fit
 */
int stow_msg_write(struct stow_msg_writer* writer, const struct stow_msg* msg);

/**
 * @brief Put the next entry; after a failure, later puts do nothing
 *
 * @param writer The writer
 * @param entry The entry, with its codec for a value
 *
 * @return SUCCESS, -E2BIG when it does not fit, or the codec's error
 */
int stow_msg_put_entry(struct stow_msg_writer* writer, const struct stow_msg_entry* entry);

/**
 * @brief Put the next name of a Describe, which keeps room to continue from any item
 *
 * @param writer The writer
 * @param name The name
 *
 * @return SUCCESS, or -E2BIG when it or an earlier name was rejected; the message stays valid
 */
int stow_msg_put_name(struct stow_msg_writer* writer, const char* name);

/**
 * @brief Put the next description of a Describe Response; the first one rejected becomes the next item
 *
 * @param writer The writer
 * @param item The item to describe
 *
 * @return SUCCESS, or -E2BIG when it or an earlier description was rejected; the message stays valid
 */
int stow_msg_put_description(struct stow_msg_writer* writer, const struct stow_item_const_metadata* item);

/**
 * @brief Finish the message
 *
 * @param writer The writer
 * @param[out] len_out Length of the message, at the start of the byte range
 *
 * @return SUCCESS
 * @return the first error of the message
 * @return -E2BIG when a Describe or Describe Response rejected its first element
 */
int stow_msg_finish(struct stow_msg_writer* writer, size_t* len_out);

/**
 * @brief Check whether a message code is a request, which only a server answers
 *
 * @param code Message code
 *
 * @return true for a request
 */
bool stow_msg_is_request(uint32_t code);

/**
 * @brief Get the code of a request's success response
 *
 * @param request A request's code
 *
 * @return The success response's code
 */
enum stow_message_code stow_msg_answer(enum stow_message_code request);

/**
 * @}
 */
