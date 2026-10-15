import re
from pathlib import Path

from preprocess import (
    BUILTIN_STRINGS,
    builtin_item,
    preprocess_items,
    preprocess_structs,
    template_environment,
)

PREFIX_RE = re.compile(r"^[a-z](?:[a-z0-9_]*[a-z0-9])?$")


def _select_items(items: list, names: list[str] | None) -> list:
    if not names:
        return items
    by_name = {item["name"]: item for item in items}
    unknown = [name for name in names if name not in by_name]
    if unknown:
        raise ValueError(f"peer schema has no item(s) {unknown}")
    wanted = set(names)
    return [item for item in items if item["name"] in wanted]


def _used_structs(items: list, structs: dict) -> dict:
    used: dict = {}

    def visit(name: str) -> None:
        if name in used or name not in structs:
            return
        for field in structs[name]["fields"]:
            if field["type"] == "STRUCT":
                visit(field["nested_struct_name"])
        used[name] = structs[name]

    for item in items:
        if item["type"] == "STRUCT":
            visit(item["struct_name"])
    return used


def _used_enums(items: list, structs: dict, enums: dict) -> dict:
    names = {
        item["constraints_dict"]["enum"] for item in items if item["type"] == "ENUM"
    }
    for struct in structs.values():
        names.update(f["enum_name"] for f in struct["fields"] if f["type"] == "ENUM")
    return {name: enum for name, enum in enums.items() if name in names}


def generate_client(
    data: dict, output_dir: Path, prefix: str, names: list[str] | None = None
) -> None:
    if not PREFIX_RE.match(prefix) or "_struct" in prefix or "_stow" in prefix:
        raise ValueError(
            f"prefix {prefix!r} must be a lowercase C identifier that does not end in '_' "
            "or contain '_struct' or '_stow'"
        )
    p = f"{prefix}_"

    enums = data.get("enums") or {}
    structs = preprocess_structs(data.get("structs") or [], enums, p)
    roles = {name: idx for idx, name in enumerate(data.get("roles") or [])}
    builtins = [builtin_item(name) for name in BUILTIN_STRINGS]
    items = _select_items(
        builtins + preprocess_items(data["items"], enums, structs, roles), names
    )
    for item in items:
        item["client_id"] = f"{p.upper()}ID_{item['upper_name']}"
        item["client_upper"] = f"{p.upper()}{item['upper_name']}"
        if item["type"] == "STRUCT":
            item["c_interface"] = structs[item["struct_name"]]["interface_sym"]
        if item["type"] == "ENUM":
            item["c_enum_values"] = f"{p}{item['constraints_dict']['enum']}_enum_values"

    used_structs = _used_structs(items, structs)
    used_enums = _used_enums(items, used_structs, enums)

    env = template_environment(p)
    context = {
        "peer": prefix,
        "items": items,
        "roles": roles,
        "enums": used_enums,
        "structs": used_structs,
    }

    inc = output_dir / "inc" / "stow"
    src = output_dir / "src" / "stow"
    inc.mkdir(parents=True, exist_ok=True)
    src.mkdir(parents=True, exist_ok=True)
    for directory in (inc, src):
        for stem in ("stow_client.", "stow_enums.", "struct_*."):
            for stale in directory.glob(f"generated_{p}{stem}[ch]"):
                stale.unlink()

    outputs = {
        inc / f"generated_{p}stow_client.h": ("generated_stow_client.h.j2", context),
        src / f"generated_{p}stow_client.c": ("generated_stow_client.c.j2", context),
        inc / f"generated_{p}stow_enums.h": ("generated_stow_enums.h.j2", context),
        src / f"generated_{p}stow_enums.c": ("generated_stow_enums.c.j2", context),
    }
    for name, struct in used_structs.items():
        struct_context = {"struct": struct, "enums": enums, "structs": structs}
        outputs[inc / f"generated_{p}struct_{name}.h"] = (
            "generated_struct_header.h.j2",
            struct_context,
        )
        outputs[src / f"generated_{p}struct_{name}.c"] = (
            "generated_struct_source.c.j2",
            struct_context,
        )

    for path, (template, template_context) in outputs.items():
        path.write_text(env.get_template(template).render(**template_context))
        print(f"Generated {path}")
