VALID_TYPES = {"STRING", "INT", "FLOAT", "ENUM", "BYTE_ARRAY", "BUFFER", "STRUCT"}
VALID_STRUCT_FIELD_TYPES = {"STRING", "INT", "FLOAT", "ENUM", "BYTE_ARRAY", "BUFFER", "STRUCT"}
VALID_STORAGE_TYPES = {"EPHEMERAL", "PERSISTENT", "TOFU"}
VALID_PERMS = {"ANY", "SESSION", "DEV", "INTERNAL", "NONE"}
ITEM_REQUIRED_FIELDS = {"name", "description", "type", "storage", "permissions", "default", "constraints", "categories"}


def _flatten(lst: list, ctx: str) -> dict:
    result = {}
    for entry in lst:
        if not isinstance(entry, dict):
            raise ValueError(f"{ctx}: expected a list of mappings, got {type(entry).__name__}")
        result.update(entry)
    return result


def _validate_structs(structs_list: list, enums: dict) -> None:
    for struct in structs_list:
        if not isinstance(struct, dict):
            raise ValueError("Each struct must be a mapping")
        name = struct.get("name", "<unnamed>")
        ctx = f"struct '{name}'"

        if "name" not in struct:
            raise ValueError(f"{ctx}: missing 'name'")
        if "description" not in struct:
            raise ValueError(f"{ctx}: missing 'description'")
        if "fields" not in struct or not isinstance(struct["fields"], list) or not struct["fields"]:
            raise ValueError(f"{ctx}: 'fields' must be a non-empty list")

        for i, field in enumerate(struct["fields"]):
            fctx = f"{ctx} field[{i}]"
            if not isinstance(field, dict):
                raise ValueError(f"{fctx}: must be a mapping")
            for k in ("name", "type", "constraints"):
                if k not in field:
                    raise ValueError(f"{fctx}: missing '{k}'")

            ftype = field["type"]
            if ftype not in VALID_STRUCT_FIELD_TYPES:
                raise ValueError(
                    f"{fctx}: invalid type '{ftype}', must be one of {sorted(VALID_STRUCT_FIELD_TYPES)}"
                )

            fcdict = _flatten(field["constraints"], f"{fctx} constraints")

            if ftype in ("STRING", "BYTE_ARRAY", "BUFFER"):
                for k in ("min_len", "max_len"):
                    if k not in fcdict:
                        raise ValueError(f"{fctx}: type {ftype} requires constraint '{k}'")
            elif ftype in ("INT", "FLOAT"):
                for k in ("min", "max"):
                    if k not in fcdict:
                        raise ValueError(f"{fctx}: type {ftype} requires constraint '{k}'")
            elif ftype == "ENUM":
                if "enum" not in fcdict:
                    raise ValueError(f"{fctx}: type ENUM requires constraint 'enum'")
                if fcdict["enum"] not in enums:
                    raise ValueError(
                        f"{fctx}: constraint 'enum' references unknown enum '{fcdict['enum']}'"
                    )
            elif ftype == "STRUCT":
                if "struct" not in fcdict:
                    raise ValueError(f"{fctx}: type STRUCT requires constraint 'struct'")
                # Note: forward-reference validation skipped here; generator handles ordering


def validate(data: dict) -> None:
    """Validate the parsed datastore YAML. Raises ValueError on failure."""
    if not isinstance(data, dict):
        raise ValueError("YAML root must be a mapping")

    enums = data.get("enums") or {}
    if not isinstance(enums, dict):
        raise ValueError("'enums' must be a mapping")

    for enum_name, enum_def in enums.items():
        ctx = f"enum '{enum_name}'"
        if not isinstance(enum_def, dict):
            raise ValueError(f"{ctx}: must be a mapping")
        if "description" not in enum_def:
            raise ValueError(f"{ctx}: missing 'description'")
        if "values" not in enum_def:
            raise ValueError(f"{ctx}: missing 'values'")
        if not isinstance(enum_def["values"], list) or not enum_def["values"]:
            raise ValueError(f"{ctx}: 'values' must be a non-empty list")
        for i, val in enumerate(enum_def["values"]):
            if not isinstance(val, dict):
                raise ValueError(f"{ctx} value[{i}]: must be a mapping")
            if "name" not in val:
                raise ValueError(f"{ctx} value[{i}]: missing 'name'")
            if "value" not in val:
                raise ValueError(f"{ctx} value[{i}]: missing 'value'")
            if not isinstance(val["value"], int):
                raise ValueError(f"{ctx} value[{i}]: 'value' must be an integer")

    categories_raw = data.get("categories") or []
    if not isinstance(categories_raw, list) or not categories_raw:
        raise ValueError("'categories' must be a non-empty list")
    for i, cat in enumerate(categories_raw):
        if not isinstance(cat, str) or not cat:
            raise ValueError(f"categories[{i}]: must be a non-empty string")
    valid_categories = set(categories_raw)

    structs_list = data.get("structs") or []
    if not isinstance(structs_list, list):
        raise ValueError("'structs' must be a list")
    _validate_structs(structs_list, enums)
    structs = {s["name"]: s for s in structs_list}

    if "items" not in data:
        raise ValueError("Missing required top-level key 'items'")
    items = data["items"]
    if not isinstance(items, list) or not items:
        raise ValueError("'items' must be a non-empty list")

    for item in items:
        if not isinstance(item, dict):
            raise ValueError("Each item must be a mapping")
        name = item.get("name", "<unnamed>")
        ctx = f"item '{name}'"

        missing = ITEM_REQUIRED_FIELDS - set(item.keys())
        if missing:
            raise ValueError(f"{ctx}: missing required fields: {', '.join(sorted(missing))}")

        item_cats = item["categories"]
        if not isinstance(item_cats, list) or not item_cats:
            raise ValueError(f"{ctx}: 'categories' must be a non-empty list")
        for cat in item_cats:
            if cat not in valid_categories:
                raise ValueError(f"{ctx}: category '{cat}' is not defined in top-level 'categories'")

        item_type = item["type"]
        if item_type not in VALID_TYPES:
            raise ValueError(f"{ctx}: invalid type '{item_type}', must be one of {sorted(VALID_TYPES)}")

        if item["storage"] not in VALID_STORAGE_TYPES:
            raise ValueError(f"{ctx}: invalid storage '{item['storage']}', must be one of {sorted(VALID_STORAGE_TYPES)}")

        perms = item["permissions"]
        if not isinstance(perms, list):
            raise ValueError(f"{ctx}: 'permissions' must be a list")
        perm_dict = _flatten(perms, f"{ctx} permissions")
        for k in ("read", "write"):
            if k not in perm_dict:
                raise ValueError(f"{ctx}: 'permissions' missing '{k}'")
            if perm_dict[k] not in VALID_PERMS:
                raise ValueError(
                    f"{ctx}: invalid permission value '{perm_dict[k]}' for '{k}', "
                    f"must be one of {sorted(VALID_PERMS)}"
                )

        constraints = item["constraints"]
        if not isinstance(constraints, list):
            raise ValueError(f"{ctx}: 'constraints' must be a list")
        cdict = _flatten(constraints, f"{ctx} constraints")

        if item_type in ("STRING", "BYTE_ARRAY", "BUFFER"):
            for k in ("min_len", "max_len"):
                if k not in cdict:
                    raise ValueError(f"{ctx}: type {item_type} requires constraint '{k}'")
        elif item_type in ("INT", "FLOAT"):
            for k in ("min", "max"):
                if k not in cdict:
                    raise ValueError(f"{ctx}: type {item_type} requires constraint '{k}'")
        elif item_type == "ENUM":
            if "enum" not in cdict:
                raise ValueError(f"{ctx}: type ENUM requires constraint 'enum'")
            enum_ref = cdict["enum"]
            if enum_ref not in enums:
                raise ValueError(
                    f"{ctx}: constraint 'enum' references unknown enum '{enum_ref}'"
                )
        elif item_type == "STRUCT":
            if "struct" not in cdict:
                raise ValueError(f"{ctx}: type STRUCT requires constraint 'struct'")
            struct_ref = cdict["struct"]
            if struct_ref not in structs:
                raise ValueError(
                    f"{ctx}: constraint 'struct' references unknown struct '{struct_ref}'"
                )
