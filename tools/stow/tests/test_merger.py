import pytest
import yaml

from merger import compose, merge_dict, merge_named_list, validate_fragment_shape

BASE_DEVICE_NAME = {
    "name": "DeviceName",
    "description": "The name of the device",
    "categories": ["General"],
    "type": "STRING",
    "storage": "PERSISTENT",
    "permissions": [{"read": "ANY"}, {"write": ["Session"]}],
    "default": "Base",
    "constraints": [{"min_len": 3}, {"max_len": 25}],
}


def write_yaml(path, data: dict):
    path.write_text(yaml.safe_dump(data, sort_keys=False))
    return path


def test_identical_item_redefinition_dedups():
    base = {"items": [dict(BASE_DEVICE_NAME)]}
    overlay = {"items": [dict(BASE_DEVICE_NAME)]}

    merged = merge_dict(base, overlay)

    assert len(merged["items"]) == 1
    assert merged["items"][0] == BASE_DEVICE_NAME


def test_field_level_item_override_inherits_unspecified_fields():
    base = {"items": [dict(BASE_DEVICE_NAME)]}
    overlay = {"items": [{"name": "DeviceName", "default": "Overridden"}]}

    merged = merge_dict(base, overlay)

    assert len(merged["items"]) == 1
    item = merged["items"][0]
    assert item["default"] == "Overridden"
    assert item["type"] == "STRING"
    assert item["storage"] == "PERSISTENT"
    assert item["constraints"] == BASE_DEVICE_NAME["constraints"]
    assert item["permissions"] == BASE_DEVICE_NAME["permissions"]


def test_constraints_sub_merge_patches_single_key():
    base = {"items": [dict(BASE_DEVICE_NAME)]}
    overlay = {"items": [{"name": "DeviceName", "constraints": [{"max_len": 10}]}]}

    merged = merge_dict(base, overlay)

    constraints = dict(kv for d in merged["items"][0]["constraints"] for kv in d.items())
    assert constraints == {"min_len": 3, "max_len": 10}


def test_merge_named_list_appends_new_entries_in_overlay_order():
    base = [{"name": "A", "value": 1}]
    overlay = [{"name": "B", "value": 2}, {"name": "A", "value": 9}]

    merged = merge_named_list(base, overlay)

    assert [e["name"] for e in merged] == ["A", "B"]
    assert merged[0]["value"] == 9
    assert merged[1]["value"] == 2


def test_roles_and_categories_ordered_union_dedup():
    base = {"roles": ["Session"], "categories": ["General", "Test"]}
    overlay = {"roles": ["Session", "Dev"], "categories": ["Test", "BLE"]}

    merged = merge_dict(base, overlay)

    assert merged["roles"] == ["Session", "Dev"]
    assert merged["categories"] == ["General", "Test", "BLE"]


def test_validate_fragment_shape_rejects_unknown_top_level_key(tmp_path):
    path = tmp_path / "bad.stow.yaml"
    with pytest.raises(ValueError, match="unknown top-level key"):
        validate_fragment_shape({"items": [], "wat": 1}, path)


def test_validate_fragment_shape_rejects_unnamed_item(tmp_path):
    path = tmp_path / "bad.stow.yaml"
    with pytest.raises(ValueError, match="must be a mapping with a 'name'"):
        validate_fragment_shape({"items": [{"description": "no name"}]}, path)


def test_compose_precedence_fragment_entry_board(tmp_path):
    fragment = write_yaml(
        tmp_path / "ble.stow.yaml",
        {"categories": ["General"], "items": [dict(BASE_DEVICE_NAME)]},
    )
    entry = write_yaml(
        tmp_path / "stow.yaml",
        {"items": [{"name": "DeviceName", "default": "EntryDefault"}]},
    )
    (tmp_path / "boards").mkdir()
    board = write_yaml(
        tmp_path / "boards" / "my_board.stow.yaml",
        {"items": [{"name": "DeviceName", "default": "BoardDefault"}]},
    )

    data, provenance = compose([fragment], entry, board)

    assert data["items"][0]["default"] == "BoardDefault"
    assert data["items"][0]["type"] == "STRING"  # inherited from fragment
    assert provenance == [fragment, entry, board]


def test_compose_without_board_file_skips_it(tmp_path):
    entry = write_yaml(tmp_path / "stow.yaml", {"items": [dict(BASE_DEVICE_NAME)]})
    missing_board = tmp_path / "boards" / "nonexistent.stow.yaml"

    data, provenance = compose([], entry, missing_board)

    assert provenance == [entry]
    assert data["items"][0]["default"] == "Base"
