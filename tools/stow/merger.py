from pathlib import Path

import yaml

KNOWN_TOP_LEVEL_KEYS = {"enums", "structs", "roles", "categories", "items"}
_MISSING = object()


def load_yaml(path: Path) -> dict:
    with open(path) as f:
        data = yaml.safe_load(f)
    return data or {}


def validate_fragment_shape(data: dict, path: Path) -> None:
    if not isinstance(data, dict):
        raise ValueError(f"{path}: fragment root must be a mapping")

    unknown = set(data.keys()) - KNOWN_TOP_LEVEL_KEYS
    if unknown:
        raise ValueError(
            f"{path}: unknown top-level key(s) {sorted(unknown)}, "
            f"expected a subset of {sorted(KNOWN_TOP_LEVEL_KEYS)}"
        )

    enums = data.get("enums")
    if enums is not None and not isinstance(enums, dict):
        raise ValueError(f"{path}: 'enums' must be a mapping")

    for section in ("structs", "items"):
        entries = data.get(section)
        if entries is None:
            continue
        if not isinstance(entries, list):
            raise ValueError(f"{path}: '{section}' must be a list")
        for i, entry in enumerate(entries):
            if not isinstance(entry, dict) or "name" not in entry:
                raise ValueError(
                    f"{path}: {section}[{i}] must be a mapping with a 'name'"
                )

    for section in ("roles", "categories"):
        entries = data.get(section)
        if entries is not None and not isinstance(entries, list):
            raise ValueError(f"{path}: '{section}' must be a list")


def _ordered_union(base: list, overlay: list) -> list:
    result = list(base)
    seen = set(base)
    for entry in overlay:
        if entry not in seen:
            result.append(entry)
            seen.add(entry)
    return result


def _merge_flat_list(base_list: list, overlay_list: list) -> list:
    """Merge two lists of single-key mappings"""
    merged: dict = {}
    for entry in base_list:
        if isinstance(entry, dict):
            merged.update(entry)
    for entry in overlay_list:
        if isinstance(entry, dict):
            merged.update(entry)
    return [{k: v} for k, v in merged.items()]


def merge_named_list(base_list: list, overlay_list: list, key: str = "name") -> list:
    """
    Merge two lists of dicts identified by `key`, field-patching entries
    that appear in both (overlay wins per-field, base fields not present in
    the overlay entry are inherited) and appending new entries in overlay
    order.
    """
    order: list = []
    by_key: dict = {}
    for entry in base_list:
        name = entry[key]
        if name not in by_key:
            order.append(name)
        by_key[name] = dict(entry)
    for entry in overlay_list:
        name = entry[key]
        if name in by_key:
            by_key[name] = _deep_merge_entry(by_key[name], entry)
        else:
            order.append(name)
            by_key[name] = dict(entry)
    return [by_key[name] for name in order]


def _deep_merge_entry(base_entry: dict, overlay_entry: dict) -> dict:
    result = dict(base_entry)
    for k, v in overlay_entry.items():
        if k in ("fields", "values") and isinstance(v, list):
            result[k] = merge_named_list(result.get(k, []), v)
        elif k in ("constraints", "permissions") and isinstance(v, list):
            result[k] = _merge_flat_list(result.get(k, []), v)
        else:
            result[k] = v
    return result


def merge_dict(base: dict, overlay: dict) -> dict:
    result = dict(base)

    if "roles" in overlay:
        result["roles"] = _ordered_union(result.get("roles", []), overlay["roles"])

    if "categories" in overlay:
        result["categories"] = _ordered_union(
            result.get("categories", []), overlay["categories"]
        )

    if "enums" in overlay:
        merged_enums = dict(result.get("enums", {}))
        for name, enum_def in overlay["enums"].items():
            if name in merged_enums:
                merged_enums[name] = _deep_merge_entry(merged_enums[name], enum_def)
            else:
                merged_enums[name] = enum_def
        result["enums"] = merged_enums

    if "structs" in overlay:
        result["structs"] = merge_named_list(
            result.get("structs", []), overlay["structs"]
        )

    if "items" in overlay:
        result["items"] = merge_named_list(result.get("items", []), overlay["items"])

    return result


def _changed_item_fields(old: dict, new: dict) -> list:
    changed = []
    for k, v in new.items():
        if old.get(k, _MISSING) != v:
            changed.append(k)
    return changed


def compose(
    fragment_paths: list[Path], entry_path: Path, board_path: Path | None
) -> tuple[dict, list[Path]]:
    """
    Compose fragment_paths (lowest precedence, in order), then entry_path's
    own content, then board_path (highest precedence, if it exists) into one
    merged stow dict.
    """
    ordered_paths = list(fragment_paths) + [entry_path]
    if board_path is not None and board_path.exists():
        ordered_paths.append(board_path)

    acc: dict = {}
    provenance: list[Path] = []
    prev_items_by_name: dict = {}

    for path in ordered_paths:
        frag = load_yaml(path)
        validate_fragment_shape(frag, path)

        for item in frag.get("items", []):
            name = item["name"]
            if name in prev_items_by_name:
                changed = _changed_item_fields(prev_items_by_name[name], item)
                if changed:
                    print(
                        f"Gantry: stow item '{name}' overridden by {path} "
                        f"(fields: {', '.join(changed)})"
                    )

        acc = merge_dict(acc, frag)
        prev_items_by_name = {it["name"]: it for it in acc.get("items", [])}
        provenance.append(path)

    return acc, provenance
