import hashlib
import re
import sys
from datetime import datetime
from pathlib import Path

import yaml
from jinja2 import Environment, FileSystemLoader

sys.path.insert(0, str(Path(__file__).parent))
from validator import validate

INTERFACE_MAP = {
    "STRING": "stow_string_interface",
    "INT": "stow_int_interface",
    "FLOAT": "stow_float_interface",
    "ENUM": "stow_enum_interface",
    "BYTE_ARRAY": "stow_byte_array_interface",
    "BUFFER": "stow_buffer_interface",
}

FIELD_DATA_MEMBER = {
    "INT": "int_value",
    "FLOAT": "float_value",
    "ENUM": "int_value",
    "BYTE_ARRAY": "buffer_value",
    "BUFFER": "buffer_value",
    "STRING": "string_value",
    "STRUCT": "raw_value",
}

FIELD_DATA_MEMBER_CAST = {
    "INT": "",
    "FLOAT": "",
    "ENUM": "",
    "BYTE_ARRAY": "(buffer_t *)",
    "BUFFER": "",
    "STRING": "",
    "STRUCT": "",
}

FIELD_C_TYPE_ENUM = {
    "INT": "STOW_ITEM_TYPE_INT",
    "FLOAT": "STOW_ITEM_TYPE_FLOAT",
    "ENUM": "STOW_ITEM_TYPE_ENUM",
    "BYTE_ARRAY": "STOW_ITEM_TYPE_BYTE_ARRAY",
    "BUFFER": "STOW_ITEM_TYPE_BUFFER",
    "STRING": "STOW_ITEM_TYPE_STRING",
    "STRUCT": "STOW_ITEM_TYPE_STRUCT",
}

def _to_snake(name: str) -> str:
    s = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", name)
    s = re.sub(r"([a-z\d])([A-Z])", r"\1_\2", s)
    return s.lower()


def _flatten(lst: list) -> dict:
    result = {}
    for entry in lst:
        result.update(entry)
    return result


def _fmt_float(v) -> str:
    s = f"{float(v):g}"
    if "." not in s and "e" not in s:
        s += ".0"
    return s + "f"


def _preprocess_struct_fields(struct_name: str, fields: list, enums: dict, structs_by_name: dict | None = None) -> list:
    if structs_by_name is None:
        structs_by_name = {}
    result = []
    for field in fields:
        field = dict(field)
        fname = field["name"]
        ftype = field["type"]
        fsnake = _to_snake(fname)
        fcdict = _flatten(field["constraints"])
        fdefine_prefix = f"{struct_name.upper()}_{fsnake.upper()}"

        nested_struct_name = ""
        nested_struct_snake = ""
        if ftype == "INT":
            c_decl = f"int32_t {fsnake}"
            interface_sym = INTERFACE_MAP["INT"]
        elif ftype == "FLOAT":
            c_decl = f"float {fsnake}"
            interface_sym = INTERFACE_MAP["FLOAT"]
        elif ftype == "ENUM":
            c_decl = f"int {fsnake}"
            interface_sym = INTERFACE_MAP["ENUM"]
        elif ftype == "BYTE_ARRAY":
            c_decl = f"""union {{
    uint8_t {fsnake}[sizeof(buffer_t) + {fdefine_prefix}_MAX_LEN] __attribute__((aligned(sizeof(void*))));
    struct {{
        uint16_t len;
        uint8_t buf[{fdefine_prefix}_MAX_LEN] __attribute__((aligned(sizeof(void*))));
    }} inline_{fsnake};
}}"""
            interface_sym = INTERFACE_MAP["BYTE_ARRAY"]
        elif ftype == "BUFFER":
            c_decl = f"buffer_t* {fsnake}"
            interface_sym = INTERFACE_MAP["BUFFER"]
        elif ftype == "STRING":
            c_decl = f"char {fsnake}[{fdefine_prefix}_MAX_LEN + 1]"
            interface_sym = INTERFACE_MAP["STRING"]
        elif ftype == "STRUCT":
            nested_struct_name = fcdict.get("struct", "")
            nested_struct_snake = _to_snake(nested_struct_name)
            c_decl = f"{nested_struct_name}_t* {fsnake}"
            interface_sym = f"stow_struct_{nested_struct_snake}_interface"
        else:
            c_decl = f"void* {fsnake}"
            interface_sym = ""

        enum_values = []
        if ftype == "ENUM":
            enum_name_ref = fcdict.get("enum", "")
            enum_values = enums.get(enum_name_ref, {}).get("values", [])

        nested_struct_fields = []
        if ftype == "STRUCT" and nested_struct_name in structs_by_name:
            nested_struct_fields = structs_by_name[nested_struct_name].get("fields", [])

        field.update(
            {
                "snake_name": fsnake,
                "c_decl": c_decl,
                "c_type_enum": FIELD_C_TYPE_ENUM.get(ftype, ""),
                "interface_sym": interface_sym,
                "data_member": FIELD_DATA_MEMBER.get(ftype, ""),
                "data_member_cast": FIELD_DATA_MEMBER_CAST.get(ftype, ""),
                "constraints_dict": fcdict,
                "define_prefix": fdefine_prefix,
                "enum_name": fcdict.get("enum", ""),
                "enum_values": enum_values,
                "nested_struct_name": nested_struct_name,
                "nested_struct_snake": nested_struct_snake,
                "nested_struct_fields": nested_struct_fields,
            }
        )
        result.append(field)
    return result


def _preprocess_structs(structs_list: list, enums: dict) -> dict:
    # First pass: build name→raw dict so nested struct refs resolve
    raw = {s["name"]: s for s in structs_list}
    result = {}
    for struct in structs_list:
        name = struct["name"]
        snake = _to_snake(name)
        fields = _preprocess_struct_fields(snake, struct["fields"], enums, result)
        result[name] = {
            "name": name,
            "snake_name": snake,
            "upper_name": snake.upper(),
            "description": struct.get("description", ""),
            "fields": fields,
            "n_fields": len(fields),
        }
    # Second pass: fill nested_struct_fields now that all structs are preprocessed
    for name, struct_def in result.items():
        for field in struct_def["fields"]:
            if field["type"] == "STRUCT" and field["nested_struct_name"] in result:
                field["nested_struct_fields"] = result[field["nested_struct_name"]]["fields"]
    return result


def _build_struct_default(prefix: str, struct_def: dict, default_list, structs: dict):
    """
    Recursively build default field entries and pre-declarations for a struct type.
    Returns (field_entries, pre_decls) where pre_decls must be emitted before the struct variable.
    """
    default_map = {}
    if isinstance(default_list, list):
        for fd in default_list:
            if isinstance(fd, dict) and "field" in fd:
                default_map[fd["field"]] = fd.get("value")

    pre_decls: list = []
    field_entries: list = []

    for field in struct_def["fields"]:
        fname = field["name"]
        ftype = field["type"]
        fsnake = field["snake_name"]
        fcdict = field["constraints_dict"]
        fval = default_map.get(fname)

        entry = dict(field)

        if ftype == "INT":
            entry["c_default_expr"] = str(fval if fval is not None else 0)
        elif ftype == "FLOAT":
            entry["c_default_expr"] = _fmt_float(fval if fval is not None else 0.0)
        elif ftype == "ENUM":
            entry["c_default_expr"] = f"{fcdict['enum']}_{fval}" if fval is not None else "0"
        elif ftype == "BYTE_ARRAY":
            bytes_list = fval if isinstance(fval, list) else []
            entry["byte_array_len"] = len(bytes_list)
            entry["c_default_expr"] = "{ " + ", ".join(f"0x{b:02X}" for b in bytes_list) + " }"
        elif ftype == "BUFFER":
            bytes_list = fval if isinstance(fval, list) else []
            c_name = f"default_{prefix}_{fsnake}"
            entry["c_default_expr"] = f"&{c_name}"
            pre_decls.append({
                "kind": "buffer",
                "c_name": c_name,
                "len": len(bytes_list),
                "bytes_hex": [f"0x{b:02X}" for b in bytes_list],
            })
        elif ftype == "STRING":
            entry["c_default_expr"] = f"\"{fval if fval is not None else ""}\""
        elif ftype == "STRUCT":
            nested_struct_name = fcdict.get("struct", "")
            nested_struct_def = structs.get(nested_struct_name, {})
            sub_prefix = f"{prefix}_{fsnake}"
            c_name = f"default_{sub_prefix}"
            entry["c_default_expr"] = f"&{c_name}"
            if nested_struct_def:
                nested_default_list = fval if isinstance(fval, list) else []
                sub_fields, sub_pre_decls = _build_struct_default(sub_prefix, nested_struct_def, nested_default_list, structs)
                pre_decls.extend(sub_pre_decls)
                pre_decls.append({
                    "kind": "struct",
                    "c_name": c_name,
                    "struct_type": nested_struct_name,
                    "fields": sub_fields,
                })

        field_entries.append(entry)

    return field_entries, pre_decls


def _perm_to_c(perm_spec, roles: dict) -> str:
    """Convert a YAML permission spec to a C bitmask expression."""
    if isinstance(perm_spec, str):
        if perm_spec == "ANY":
            return "STOW_ROLE_ANY"
        if perm_spec == "INTERNAL":
            return "STOW_ROLE_INTERNAL"
        return f"STOW_ROLE_{perm_spec.upper()}"
    if isinstance(perm_spec, list):
        if not perm_spec:
            return "STOW_ROLE_INTERNAL"
        return " | ".join(f"STOW_ROLE_{r.upper()}" for r in perm_spec)
    raise ValueError(f"Invalid permission spec: {perm_spec!r}")


def _preprocess_items(items: list, enums: dict, structs: dict, roles: dict | None = None) -> list:
    if roles is None:
        roles = {}
    result = []
    for item in items:
        item = dict(item)
        name = item["name"]
        snake = _to_snake(name)
        upper = snake.upper()

        item_categories = item.get("categories", [])
        item_categories_snake = [_to_snake(c) for c in item_categories]

        item_type = item["type"]
        default = item["default"]

        perm_dict = _flatten(item["permissions"])
        cdict = _flatten(item["constraints"])

        # Extract struct_name early so STRUCT branch can use it
        struct_name = cdict.get("struct", "")

        if item_type == "STRING":
            c_default = f'.string_value = "{default}"'
            c_field_decl = f"char {snake}[{upper}_MAX_LEN + 1]"
        elif item_type == "INT":
            c_default = f".int_value = {default}"
            c_field_decl = f"int {snake}"
        elif item_type == "FLOAT":
            c_default = f".float_value = {_fmt_float(default)}"
            c_field_decl = f"float {snake}"
        elif item_type == "ENUM":
            enum_name = cdict["enum"]
            c_default = f".int_value = {enum_name}_{default}"
            c_field_decl = f"int {snake}"
        elif item_type == "BYTE_ARRAY":
            c_default = f".buffer_value = &default_{snake}"
            c_field_decl = f"""union {{
    uint8_t {snake}[sizeof(buffer_t) + {upper}_MAX_LEN] __attribute__((aligned(sizeof(void*))));
    struct {{
        uint16_t len;
        uint8_t buf[{upper}_MAX_LEN] __attribute__((aligned(sizeof(void*))));
    }} inline_{snake};
}}"""
        elif item_type == "STRUCT":
            c_default = f".raw_value = &default_{snake}"
            c_field_decl = f"{struct_name}_t* {snake}"
        else:  # BUFFER
            c_default = f".buffer_value = &default_{snake}"
            c_field_decl = f"buffer_t* {snake}"

        needs_static_default = item_type in ("BYTE_ARRAY", "BUFFER", "STRUCT")
        if item_type == "BYTE_ARRAY":
            default_bytes = default if isinstance(default, list) else []
            default_bytes_hex = [f"0x{b:02X}" for b in default_bytes]
            default_bytes_len = len(default_bytes)
        elif item_type == "BUFFER":
            default_bytes = default if isinstance(default, list) else []
            default_bytes_hex = [f"0x{b:02X}" for b in default_bytes]
            default_bytes_len = len(default_bytes)
        else:
            default_bytes_hex = []
            default_bytes_len = 0

        # STRUCT-specific
        struct_def = structs.get(struct_name, {})
        struct_default_fields = []
        struct_pre_decls: list = []
        if item_type == "STRUCT" and struct_def:
            struct_default_fields, struct_pre_decls = _build_struct_default(snake, struct_def, default, structs)

        # Determine interface symbol
        if item_type == "STRUCT":
            c_interface = f"stow_struct_{_to_snake(struct_name)}_interface"
        else:
            c_interface = INTERFACE_MAP[item_type]

        item.update(
            {
                "snake_name": snake,
                "upper_name": upper,
                "item_categories": item_categories,
                "item_categories_snake": item_categories_snake,
                "id_enum": f"STOW_ID_{upper}",
                "c_type_enum": f"STOW_ITEM_TYPE_{item_type}",
                "c_storage_enum": f"STOW_STORAGE_{item['storage']}",
                "c_interface": c_interface,
                "c_read_perm": _perm_to_c(perm_dict['read'], roles),
                "c_write_perm": _perm_to_c(perm_dict['write'], roles),
                "constraints_dict": cdict,
                "needs_static_default": needs_static_default,
                "c_default": c_default,
                "c_field_decl": c_field_decl,
                "referenced_enum": enums.get(cdict.get("enum", ""), {}),
                "default_bytes_hex": default_bytes_hex,
                "default_bytes_len": default_bytes_len,
                "struct_name": struct_name,
                "struct_def": struct_def,
                "struct_default_fields": struct_default_fields,
                "struct_pre_decls": struct_pre_decls,
            }
        )
        result.append(item)
    return result


def _make_builtin_string_item(name: str, description: str, default: str, min_len: int, max_len: int) -> dict:
    snake = _to_snake(name)
    upper = snake.upper()
    return {
        "name": name,
        "snake_name": snake,
        "upper_name": upper,
        "description": description,
        "categories": [],
        "item_categories": [],
        "item_categories_snake": [],
        "type": "STRING",
        "storage": "EPHEMERAL",
        "id_enum": f"STOW_ID_{upper}",
        "c_type_enum": "STOW_ITEM_TYPE_STRING",
        "c_storage_enum": "STOW_STORAGE_EPHEMERAL",
        "c_interface": INTERFACE_MAP["STRING"],
        "c_read_perm": "STOW_ROLE_ANY",
        "c_write_perm": "STOW_ROLE_INTERNAL",
        "constraints_dict": {"min_len": min_len, "max_len": max_len},
        "needs_static_default": False,
        "c_default": f'.string_value = "{default}"',
        "c_field_decl": f"char {snake}[{upper}_MAX_LEN + 1]",
        "referenced_enum": {},
        "default_bytes_hex": [],
        "default_bytes_len": 0,
        "struct_name": "",
        "struct_def": {},
        "struct_default_fields": [],
        "struct_pre_decls": [],
    }


def _parse_version_file(text: str) -> str:
    """Parse a VERSION file into a semver string."""
    kv: dict[str, str] = {}
    for line in text.splitlines():
        line = line.strip()
        if "=" in line and not line.startswith("#"):
            key, _, val = line.partition("=")
            kv[key.strip()] = val.strip()

    major = kv.get("VERSION_MAJOR", "0")
    minor = kv.get("VERSION_MINOR", "0")
    patch = kv.get("PATCHLEVEL", "0")
    extra = kv.get("EXTRAVERSION", "")
    version = f"{major}.{minor}.{patch}"
    if extra:
        version += f"-{extra}"
    return version


def _build_builtin_items(yaml_path: Path) -> list:
    items = []
    version_path = yaml_path.parent / "VERSION"

    stow_bytes = yaml_path.read_bytes()
    if version_path.exists():
        stow_bytes += version_path.read_bytes()
    yaml_hash = hashlib.sha256(stow_bytes).hexdigest()
    items.append(_make_builtin_string_item(
        "StowHash",
        "SHA256 hash of the Stow",
        yaml_hash,
        64,
        64,
    ))

    version_path = yaml_path.parent / "VERSION"
    if version_path.exists():
        version = _parse_version_file(version_path.read_text())
        items.append(_make_builtin_string_item(
            "FirmwareVersion",
            "Firmware version",
            version,
            1,
            32,
        ))

    return items


def generate(yaml_path: Path, output_dir: Path) -> None:
    with open(yaml_path) as f:
        data = yaml.safe_load(f)

    validate(data)

    enums = data.get("enums") or {}
    structs_list = data.get("structs") or []
    structs = _preprocess_structs(structs_list, enums)
    categories_list = data.get("categories") or []
    categories_meta = {name: _to_snake(name) for name in categories_list}
    roles_list = data.get("roles") or []
    roles = {name: idx for idx, name in enumerate(roles_list)}
    builtin_items = _build_builtin_items(yaml_path)
    items = builtin_items + _preprocess_items(data["items"], enums, structs, roles)

    referenced_enum_names = []
    seen: set = set()
    for item in items:
        if item["type"] == "ENUM":
            enum_ref = item["constraints_dict"]["enum"]
            if enum_ref not in seen:
                referenced_enum_names.append(enum_ref)
                seen.add(enum_ref)

    template_dir = Path(__file__).parent / "templates"
    env = Environment(
        loader=FileSystemLoader(str(template_dir)),
        trim_blocks=True,
        lstrip_blocks=True,
        keep_trailing_newline=True,
    )
    env.globals["now"] = datetime.now().strftime("%Y-%m-%d")

    context = {
        "enums": enums,
        "structs": structs,
        "items": items,
        "referenced_enum_names": referenced_enum_names,
        "categories_meta": categories_meta,
        "roles": roles,
    }

    output_dir.mkdir(parents=True, exist_ok=True)

    h_out = output_dir / "inc" / "stow" / "generated_stow_items.h"
    c_out = output_dir / "src" / "stow" / "generated_stow_items.c"
    enums_h_out = output_dir / "inc" / "stow" / "generated_stow_enums.h"
    enums_c_out = output_dir / "src" / "stow" / "generated_stow_enums.c"

    h_out.write_text(env.get_template("generated_stow_items.h.j2").render(**context))
    c_out.write_text(env.get_template("generated_stow_items.c.j2").render(**context))
    enums_h_out.write_text(env.get_template("generated_stow_enums.h.j2").render(**context))
    enums_c_out.write_text(env.get_template("generated_stow_enums.c.j2").render(**context))

    print(f"Generated {h_out}")
    print(f"Generated {c_out}")
    print(f"Generated {enums_h_out}")
    print(f"Generated {enums_c_out}")

    # Generate per-struct interface files
    struct_template_h = env.get_template("generated_struct_header.h.j2")
    struct_template_c = env.get_template("generated_struct_source.c.j2")

    for struct_name, struct_def in structs.items():
        struct_h_out = output_dir / "inc" / "stow" / f"generated_struct_{struct_name}.h"
        struct_c_out = output_dir / "src" / "stow" / f"generated_struct_{struct_name}.c"

        struct_context = {"struct": struct_def, "enums": enums, "structs": structs}

        struct_h_out.write_text(struct_template_h.render(**struct_context))
        struct_c_out.write_text(struct_template_c.render(**struct_context))

        print(f"Generated {struct_h_out}")
        print(f"Generated {struct_c_out}")
