import re
import sys
from datetime import datetime
from pathlib import Path

import yaml
from jinja2 import Environment, FileSystemLoader

sys.path.insert(0, str(Path(__file__).parent))
from validator import validate

INTERFACE_MAP = {
    "STRING": "datastore_string_interface",
    "INT": "datastore_int_interface",
    "FLOAT": "datastore_float_interface",
    "ENUM": "datastore_enum_interface",
    "BYTE_ARRAY": "datastore_byte_array_interface",
    "BUFFER": "datastore_buffer_interface",
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


def _preprocess_items(items: list, enums: dict) -> list:
    result = []
    for item in items:
        item = dict(item)
        name = item["name"]
        snake = _to_snake(name)
        upper = snake.upper()
        item_type = item["type"]
        default = item["default"]

        perm_dict = _flatten(item["permissions"])
        cdict = _flatten(item["constraints"])

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
            c_field_decl = f"uint8_t {snake}[sizeof(buffer_t) + {upper}_MAX_LEN]"
        else:  # BUFFER
            c_default = f".buffer_value = &default_{snake}"
            c_field_decl = f"buffer_t* {snake}"

        needs_static_default = item_type in ("BYTE_ARRAY", "BUFFER")
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

        item.update(
            {
                "snake_name": snake,
                "upper_name": upper,
                "id_enum": f"DATASTORE_ID_{upper}",
                "c_type_enum": f"DATASTORE_ITEM_TYPE_{item_type}",
                "c_storage_enum": f"DATASTORE_STORAGE_{item['storage']}",
                "c_interface": INTERFACE_MAP[item_type],
                "c_read_perm": f"AUTH_{perm_dict['read']}",
                "c_write_perm": f"AUTH_{perm_dict['write']}",
                "constraints_dict": cdict,
                "needs_static_default": needs_static_default,
                "c_default": c_default,
                "c_field_decl": c_field_decl,
                "referenced_enum": enums.get(cdict.get("enum", ""), {}),
                "default_bytes_hex": default_bytes_hex,
                "default_bytes_len": default_bytes_len,
            }
        )
        result.append(item)
    return result


def generate(yaml_path: Path, output_dir: Path) -> None:
    with open(yaml_path) as f:
        data = yaml.safe_load(f)

    validate(data)

    enums = data.get("enums") or {}
    items = _preprocess_items(data["items"], enums)

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
        "items": items,
        "referenced_enum_names": referenced_enum_names,
    }

    h_out = output_dir / "inc" / "datastore" / "generated_datastore_items.h"
    c_out = output_dir / "src" / "datastore" / "generated_datastore_items.c"

    h_out.write_text(env.get_template("generated_datastore_items.h.j2").render(**context))
    c_out.write_text(env.get_template("generated_datastore_items.c.j2").render(**context))

    print(f"Generated {h_out}")
    print(f"Generated {c_out}")
