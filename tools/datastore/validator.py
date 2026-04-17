VALID_TYPES = {"STRING", "INT", "FLOAT", "ENUM", "BYTE_ARRAY", "BUFFER"}
VALID_STORAGE_TYPES = {"EPHEMERAL", "PERSISTENT", "TOFU"}
VALID_PERMS = {"ANY", "SESSION", "DEV", "INTERNAL", "NONE"}
ITEM_REQUIRED_FIELDS = {"name", "description", "type", "storage", "permissions", "default", "constraints"}


def _flatten(lst: list, ctx: str) -> dict:
    result = {}
    for entry in lst:
        if not isinstance(entry, dict):
            raise ValueError(f"{ctx}: expected a list of mappings, got {type(entry).__name__}")
        result.update(entry)
    return result


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
