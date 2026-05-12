# Stow Protocol

The Stow protocol allows external devices to interact with the Stow.

## Messages

All messages are encoded using CBOR. The examples for each message will be represented using JSON syntax, use a tool such as [cbor.me](https://cbor.me/) to view the CBOR representation.

### Version

The version command requests the Stow protocol version, currently `1`.

```json
[0]
```

### Version Response

The current Stow protocol version (always `1`).

```json
[1, <version>]
```

### Describe

Start a request for a description of the Stow's items. The Stow client should send this request to start a Stow description and receive the first chunk.

```json
[2]
```

### Describe Next

Request the next chunk of the Stow's description. The Stow client should keep sending this request until the Stow has been fully described. The client will know that the Stow has been fully described when the associated `Describe Response` returns an empty byte string.

```json
[3]
```

### Describe Response

A chunk of the Stow description. The Stow description is itself encoded using CBOR. When transmitted as a describe chunk this is placed in a byte string. This is because the CBOR encoding may be partially incomplete since the description is chunked.

```json
[4, <byte string containing a chunk of the CBOR description>]
```

### Get

Get the value of an item in the Stow.

```json
[5, <item ID>]
```

### Get Response

A response containing the requested item's value.

```json
[6, <item ID>, <value>]
```

### Set

Set the value of an item in the Stow. The client will receive an `OK` response if the operation was successful. Otherwise, an `Error` response will be sent.

```json
[7, <item ID>, <value>]
```

### Subscribe

Subscribe to an item in the Stow. The client will be notified via an `Update` message when the value changes. The client will receive an `OK` response if the operation was successful. Otherwise, an `Error` response will be sent.

```json
[8, <item ID>]
```

### Unsubscribe

Unsubscribe from an item in the Stow. The client will receive an `OK` response if the operation was successful. Otherwise, an `Error` response will be sent.

```json
[9, <item ID>]
```

### Update

A message containing the updated value of an item. These messages are sent to the client when they are subscribed to the associated item.

```json
[10, <item ID>, <value>]
```

### OK

A response sent when an operation succeeds that does not otherwise have a dedicated response.

```json
[11]
```

### Error

A response sent when an operation fails. The message contains a text description of the error.

```json
[12, <error message>]
```

## Stow Description

Once the client has obtained the Stow description via `Describe` and `Describe Next`, the chunked payloads of `Describe Response` can be combined into a CBOR encoded list of item descriptions.

Each item description has the following format:

```json
{
    "id": <item ID>,
    "name": <item name>,
    "categories": [<category>, ...],
    "storage": <storage type>,
    "read_perm": <read permissions>,
    "write_perm": <write permissions>,
    "type": <item type>,
    "default": <default value>,
    "constraints": <item value constraints>,
}
```

### ID

The `id` is the numeric ID used to access a specific item when using the Stow Protocol. The `id` may change across firmware versions. The permanent and human readable identifier for an item is its `name`.

### Categories

Each item will belong to at least one category. Categories are just strings to help clients group related items together in user interfaces. They have no functional purpose within the Stow.

### Storage

The `storage` type is a string indicating whether the items value is "Ephemeral" (it will reset across reboots), "Persistent" (it will persist across reboots), or "TOFU" (it will persist and can only be changed once).

### Permissions

`read_perm` and `write_perm` are strings indicated the access level required to interact with the item. The access levels are "Any" (any client), "Session" (only authenticated clients), "Dev" (a developer session is required), and "No access" (the item can only be accessed internally by the device firmware or is a constant value).

### Type

`type` is a string describing the datatype of the item. Currently, the supported types are: "Enum", "Int", "Float", "String", "Byte Array", "Buffer", and "Struct".

### Default

The `default` value is encoded based on the item's `type`. The following is a list of how each type is encoded via CBOR:

- Enum: an int32 representing the numeric value of the enum
- Int: an int32
- Float: a float32
- String: a text string
- Byte Array: a binary string
- Buffer: a binary string
- Struct: a list containing the fields of the struct. Each field is encoded based on that field's datatype. For example, a string with a length integer and a string may be encoded as `[5, "Hello"]`. Note that structs can contain other structs so there may be nested lists.

### Constraints

The `constraints` of an item describe the possible values or value ranges for the given item. In the case of structs, the constraints double as a description of the fields within the struct. Each of these field has its own constraints.

#### Enum

Enum constraints are a list of possible enum value paired with descriptive names. The names allow Stow clients to describe the meaning of each value.

```json
[
    {
        "value": <int32 value>,
        "name": <text string>
    },
    ...
]
```

#### Int & Float

Integers and float constraints are just minimum and maximum values encoded as int32's or float32's, respectively.

```json
{
    "min": <min value>,
    "max": <max value>
}
```

#### String, Byte Array, & Buffer

Strings, byte arrays, and buffer constraints are the minimum and maximum lengths of the buffer.

```json
{
    "min_len": <min length>,
    "max_len": <max length>
}
```

#### Structs

Struct constraints are a list of fields. Each field contains the field's name, type, and individual constraints. The constraints for each field are based on the fields type. The fields can be any Stow item type, including other structs.

```json
[
    {
        "name": <field name>,
        "type": <field type>,
        "constraints": <field constraints>
    },
    ...
]
```
