"""Render the shared golden vectors as a C header for the protocol test suite."""

import argparse
import json
from pathlib import Path


def _bytes(name: str, hex_string: str) -> str:
    data = bytes.fromhex(hex_string)
    body = ", ".join(f"0x{b:02X}" for b in data)
    return f"static const uint8_t {name}[] = {{ {body} }};"


def _roles(roles: list) -> str:
    if not roles:
        return "0"
    return " | ".join(f"STOW_ROLE_{role.upper()}" for role in roles)


def _bool(value: bool) -> str:
    return "true" if value else "false"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vectors", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    declarations = []
    entries = []
    for vector in json.loads(args.vectors.read_text())["vectors"]:
        name = vector["name"]
        setup = vector.get("setup", [])
        for index, request in enumerate(setup):
            declarations.append(_bytes(f"{name}_setup_{index}", request))
        if setup:
            steps = ", ".join(
                f"{{ {name}_setup_{index}, sizeof({name}_setup_{index}) }}"
                for index in range(len(setup))
            )
            declarations.append(
                f"static const struct test_vector_step {name}_setup[] = {{ {steps} }};"
            )
        declarations.append(_bytes(f"{name}_request", vector["request"]))
        declarations.append(_bytes(f"{name}_response", vector["response"]))
        setup_field = f"{name}_setup" if setup else "NULL"
        entries.append(
            "    {\n"
            f'        .name = "{name}",\n'
            f"        .meaning = {json.dumps(vector['meaning'])},\n"
            f"        .roles = {_roles(vector['roles'])},\n"
            f"        .setup = {setup_field},\n"
            f"        .setup_count = {len(setup)},\n"
            f"        .request = {name}_request,\n"
            f"        .request_len = sizeof({name}_request),\n"
            f"        .response = {name}_response,\n"
            f"        .response_len = sizeof({name}_response),\n"
            f"        .client_sends = {_bool(vector.get('client_sends', True))},\n"
            f"        .invalid_request = {_bool(vector.get('invalid_request', False))},\n"
            "    },"
        )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        "#pragma once\n\n"
        "#include <generated_stow_items.h>\n"
        "#include <stdbool.h>\n"
        "#include <stddef.h>\n"
        "#include <stdint.h>\n"
        "#include <zephyr/sys/util.h>\n\n"
        "/**\n * @brief A request sent before a vector's own, which must answer OK\n */\n"
        "struct test_vector_step\n{\n"
        "    const uint8_t* request; /**< Encoded request */\n"
        "    size_t request_len;     /**< Length of request */\n"
        "};\n\n"
        "/**\n * @brief A golden vector: a request and the exact response it gets\n */\n"
        "struct test_vector\n{\n"
        "    const char* name;                     /**< Vector name */\n"
        "    const char* meaning;                  /**< What the vector shows */\n"
        "    stow_role_t roles;                    /**< Roles of the session */\n"
        "    const struct test_vector_step* setup; /**< Requests sent first, or NULL */\n"
        "    size_t setup_count;                   /**< Number of setup steps */\n"
        "    const uint8_t* request;               /**< Encoded request */\n"
        "    size_t request_len;                   /**< Length of request */\n"
        "    const uint8_t* response;              /**< Encoded expected response */\n"
        "    size_t response_len;                  /**< Length of response */\n"
        "    bool client_sends;                    /**< A correct client may send the request */\n"
        "    bool invalid_request;                 /**< The request breaks the protocol's CDDL */\n"
        "};\n\n"
        + "\n".join(declarations)
        + "\n\nstatic const struct test_vector test_vectors[] = {\n"
        + "\n".join(entries)
        + "\n};\n"
    )


if __name__ == "__main__":
    main()
