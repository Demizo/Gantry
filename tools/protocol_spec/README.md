# protocol-spec

Checks and renders the golden vectors of a protocol folder, such as `protocols/stow/`.

```yaml
cddl: stow.cddl
sides:
  client: request
  server: response
values:
  stow_hash: [uv, run, ../../tools/stow/generate_stow.py, --print-hash, --yaml, schema.yaml]
```

## Vectors

A vector is an exchange: a script of messages between the two sides on one fresh session.

```yaml
exchanges:
  - name: set_already_set
    meaning: A trust-on-first-use item that is already set is ALREADY_SET
    roles: [Guest]
    steps:
      - client: '[6 /set/, 1, 6 /TestTofu/, "first"]'
      - server: '[10 /ok/, 1]'
      - client: '[6 /set/, 19, 6 /TestTofu/, "second"]'
      - server: '[11 /error/, 19, 8 /already-set/, 6, 0]'

malformed:
  - name: malformed
    meaning: An undecodable request is answered with a null tag
    roles: [Guest]
    steps:
      - client_hex: ff
      - server: '[11 /error/, null, 1 /malformed-msg/]'
```

- Each vector has a `name`, unique across both sections, a `meaning`, the session's `roles` and its `steps`.
- A step is keyed by its side and holds one message in CBOR diagnostic notation. `/name/` comments are prose and aren't checked. `_2` pins a float to float32.
- `<side>_hex` gives raw bytes instead. It is allowed only as the first step of a `malformed` vector.
- `$name` is replaced by the output of the folder's `values` command of that name.

### Sections

- In `exchanges`, every step matches its side's rule.
- In `malformed`, the first step breaks its side's rule, and the tool checks that it does. Every other step matches. Only implementations of the second side play `malformed` vectors.

## Playing a vector

Every consumer follows the same rules:

1. Each vector runs on a fresh session with its `roles`.
2. The side under test produces exactly the bytes of its steps, in order.
3. After the last step, the side under test sends nothing more.

## Command line

| Command | Does |
| --- | --- |
| `protocol-spec check FOLDER` | Validates every vector and fails when an alternative of a side's rule has no step |
| `protocol-spec c FOLDER --prefix P --output FILE` | Renders C data, `P_sides`, `P_exchanges` and `P_malformed` with their counts, declared by `tests/common/protocol_vectors.h` |
| `protocol-spec json FOLDER --output FILE` | Exports the vectors in the shape of `vectors.yaml`, each message as `{"hex": ..., "edn": ...}` |

Python consumers call `protocol_spec.load(folder)`, which reads and validates the folder without writing a file. A load error names every bad step, such as `set_already_set.steps[1]: does not match response: ...`.
