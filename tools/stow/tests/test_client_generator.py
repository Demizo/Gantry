import copy

import pytest

from client_generator import generate_client

ITEM = {
    "description": "An item",
    "categories": [],
    "storage": "EPHEMERAL",
    "permissions": [{"read": "ANY"}, {"write": "ANY"}],
}

SCHEMA = {
    "roles": ["Guest", "Dev"],
    "enums": {
        "Mode": {
            "description": "A mode",
            "values": [{"name": "OFF", "value": 0}, {"name": "ON", "value": 1}],
        },
        "Unused": {"description": "Not bound", "values": [{"name": "A", "value": 0}]},
    },
    "structs": [
        {
            "name": "Inner",
            "description": "Nested",
            "fields": [
                {"name": "Mode", "type": "ENUM", "constraints": [{"enum": "Mode"}]}
            ],
        },
        {
            "name": "Outer",
            "description": "Holds Inner",
            "fields": [
                {"name": "In", "type": "STRUCT", "constraints": [{"struct": "Inner"}]}
            ],
        },
        {
            "name": "Other",
            "description": "Not bound",
            "fields": [
                {"name": "X", "type": "INT", "constraints": [{"min": 0}, {"max": 1}]}
            ],
        },
    ],
    "items": [
        dict(
            ITEM,
            name="Level",
            type="INT",
            default=0,
            constraints=[{"min": 0}, {"max": 9}],
        ),
        dict(
            ITEM,
            name="Shape",
            type="STRUCT",
            default=[],
            constraints=[{"struct": "Outer"}],
        ),
        dict(
            ITEM,
            name="Spare",
            type="STRUCT",
            default=[],
            constraints=[{"struct": "Other"}],
        ),
    ],
}


def generated(tmp_path, names=None, local=None):
    generate_client(SCHEMA, tmp_path, "peer", names, local=local)
    return {
        path.name: path.read_text() for path in tmp_path.rglob("*") if path.is_file()
    }


def test_binds_only_the_named_items_and_what_they_use(tmp_path):
    files = generated(tmp_path, ["Shape"])

    header = files["generated_peer_stow_client.h"]
    assert "PEER_ID_SHAPE" in header
    assert "PEER_ID_LEVEL" not in header
    assert "generated_peer_struct_Outer.h" in files
    assert "generated_peer_struct_Inner.h" in files
    assert "generated_peer_struct_Other.h" not in files
    assert "peer_Mode_ON" in files["generated_peer_stow_enums.h"]
    assert "Unused" not in files["generated_peer_stow_enums.h"]


def test_every_name_is_prefixed(tmp_path):
    files = generated(tmp_path)

    assert "} peer_Outer_t;" in files["generated_peer_struct_Outer.h"]
    assert "peer_Inner_t* in;" in files["generated_peer_struct_Outer.h"]
    assert "peer_stow_struct_outer_interface" in files["generated_peer_stow_client.c"]
    assert "#define PEER_LEVEL_MAX 9" in files["generated_peer_stow_client.h"]
    assert "#define PEER_ROLE_DEV" in files["generated_peer_stow_client.h"]
    assert "peer_Mode_enum_values" in files["generated_peer_struct_Inner.c"]


def test_builtin_items_are_bound_with_every_item(tmp_path):
    header = generated(tmp_path)["generated_peer_stow_client.h"]

    assert "PEER_ID_STOW_HASH" in header
    assert "PEER_ID_FIRMWARE_VERSION" in header


def test_builtin_items_are_bound_from_a_list_only_when_named(tmp_path):
    assert (
        "STOW_HASH"
        not in generated(tmp_path, ["Level"])["generated_peer_stow_client.h"]
    )
    assert (
        "PEER_ID_STOW_HASH"
        in generated(tmp_path, ["StowHash"])["generated_peer_stow_client.h"]
    )


def test_outputs_from_an_earlier_run_are_removed(tmp_path):
    generated(tmp_path, ["Shape", "Spare"])
    files = generated(tmp_path, ["Level"])

    assert not any("struct" in name for name in files)
    assert "generated_peer_stow_client.h" in files


def test_an_unknown_item_name_fails(tmp_path):
    with pytest.raises(ValueError, match="Levle"):
        generate_client(SCHEMA, tmp_path, "peer", ["Levle"])


@pytest.mark.parametrize(
    "prefix", ["", "Peer", "peer_", "9peer", "peer_struct", "peer_stow"]
)
def test_a_prefix_must_be_a_lowercase_identifier(tmp_path, prefix):
    with pytest.raises(ValueError, match="prefix"):
        generate_client(SCHEMA, tmp_path, prefix)


def test_identical_local_types_are_reused(tmp_path):
    files = generated(tmp_path, ["Shape"], local=SCHEMA)

    assert "typedef Outer_t peer_Outer_t;" in files["generated_peer_struct_Outer.h"]
    assert "typedef Inner_t peer_Inner_t;" in files["generated_peer_struct_Inner.h"]
    assert "generated_peer_struct_Outer.c" not in files
    assert "#define peer_Mode_ON Mode_ON" in files["generated_peer_stow_enums.h"]
    assert "#define peer_Mode Mode" in files["generated_peer_stow_enums.h"]
    assert "peer_Mode_enum_values[" not in files["generated_peer_stow_enums.c"]


def test_a_local_type_that_differs_anywhere_is_not_reused(tmp_path):
    local = copy.deepcopy(SCHEMA)
    local["enums"]["Mode"]["values"].append({"name": "AUTO", "value": 2})
    files = generated(tmp_path, ["Shape"], local=local)

    assert "} peer_Outer_t;" in files["generated_peer_struct_Outer.h"]
    assert "} peer_Inner_t;" in files["generated_peer_struct_Inner.h"]
    assert "peer_Mode_enum_values[" in files["generated_peer_stow_enums.c"]


def test_an_identical_nested_struct_is_reused_inside_a_differing_struct(tmp_path):
    local = copy.deepcopy(SCHEMA)
    local["structs"][1]["fields"].append(
        {"name": "Extra", "type": "INT", "constraints": [{"min": 0}, {"max": 1}]}
    )
    files = generated(tmp_path, ["Shape"], local=local)

    assert "} peer_Outer_t;" in files["generated_peer_struct_Outer.h"]
    assert "typedef Inner_t peer_Inner_t;" in files["generated_peer_struct_Inner.h"]
