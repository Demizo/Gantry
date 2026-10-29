import hashlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from preprocess import (
    builtin_item,
    preprocess_items,
    preprocess_structs,
    template_environment,
    to_snake,
)


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


def _build_builtin_items(hash_bytes: bytes, version_dir: Path) -> list:
    items = []
    version_path = version_dir / "VERSION"

    stow_bytes = hash_bytes
    if version_path.exists():
        stow_bytes += version_path.read_bytes()
    yaml_hash = hashlib.sha256(stow_bytes).hexdigest()
    items.append(builtin_item("StowHash", yaml_hash))

    if version_path.exists():
        version = _parse_version_file(version_path.read_text())
        items.append(builtin_item("FirmwareVersion", version))

    return items


def generate(
    data: dict, output_dir: Path, hash_bytes: bytes, version_dir: Path
) -> None:
    enums = data.get("enums") or {}
    structs_list = data.get("structs") or []
    structs = preprocess_structs(structs_list, enums)
    categories_list = data.get("categories") or []
    categories_meta = {name: to_snake(name) for name in categories_list}
    roles_list = data.get("roles") or []
    roles = {name: idx for idx, name in enumerate(roles_list)}
    builtin_items = _build_builtin_items(hash_bytes, version_dir)
    items = builtin_items + preprocess_items(data["items"], enums, structs, roles)

    # Deduplicate category sets so identical sets share one C array
    category_sets: list = []
    category_set_to_name: dict = {}
    for item in items:
        categories = tuple(item["item_categories_snake"])
        if categories:
            if categories not in category_set_to_name:
                name = f"g_stow_category_set_{len(category_sets)}"
                category_set_to_name[categories] = name
                category_sets.append({"name": name, "categories": list(categories)})
            item["categories_array_name"] = category_set_to_name[categories]
        else:
            item["categories_array_name"] = "NULL"

    referenced_enum_names = []
    seen: set = set()
    for item in items:
        if item["type"] == "ENUM":
            enum_ref = item["constraints_dict"]["enum"]
            if enum_ref not in seen:
                referenced_enum_names.append(enum_ref)
                seen.add(enum_ref)

    custom_get_externs: list = []
    custom_set_externs: list = []
    custom_validate_externs: list = []
    get_seen: set = set()
    set_seen: set = set()
    validate_seen: set = set()
    for item in items:
        sym = item.get("custom_get", "")
        if sym and sym not in get_seen:
            custom_get_externs.append(sym)
            get_seen.add(sym)
        sym = item.get("custom_set", "")
        if sym and sym not in set_seen:
            custom_set_externs.append(sym)
            set_seen.add(sym)
        sym = item.get("custom_validate", "")
        if sym and sym not in validate_seen:
            custom_validate_externs.append(sym)
            validate_seen.add(sym)

    env = template_environment()

    context = {
        "enums": enums,
        "structs": structs,
        "items": items,
        "referenced_enum_names": referenced_enum_names,
        "categories_meta": categories_meta,
        "roles": roles,
        "custom_get_externs": custom_get_externs,
        "custom_set_externs": custom_set_externs,
        "custom_validate_externs": custom_validate_externs,
        "category_sets": category_sets,
    }

    output_dir.mkdir(parents=True, exist_ok=True)

    h_out = output_dir / "inc" / "stow" / "generated_stow_items.h"
    c_out = output_dir / "src" / "stow" / "generated_stow_items.c"
    enums_h_out = output_dir / "inc" / "stow" / "generated_stow_enums.h"
    enums_c_out = output_dir / "src" / "stow" / "generated_stow_enums.c"

    h_out.write_text(env.get_template("generated_stow_items.h.j2").render(**context))
    c_out.write_text(env.get_template("generated_stow_items.c.j2").render(**context))
    enums_h_out.write_text(
        env.get_template("generated_stow_enums.h.j2").render(**context)
    )
    enums_c_out.write_text(
        env.get_template("generated_stow_enums.c.j2").render(**context)
    )

    print(f"Generated {h_out}")
    print(f"Generated {c_out}")
    print(f"Generated {enums_h_out}")
    print(f"Generated {enums_c_out}")

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
