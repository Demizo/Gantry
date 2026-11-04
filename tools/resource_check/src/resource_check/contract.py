from dataclasses import dataclass

from tree_sitter import Node

from .annotations import RETURN_TARGET, Annotations, Tag, describe_declaration
from .ast_nodes import (
    dereferenced_root,
    is_assignment,
    is_returned,
    strip_parentheses,
    text,
    uncast_identifier,
)
from .dataflow import Event, EventsOf


@dataclass(frozen=True)
class Contract:
    """A definition's own tags, with each parameter target named as the definition names it."""

    acquires: dict[str, Tag]
    releases: dict[str, Tag]
    returns: Tag | None = None

    @property
    def conditions(self) -> set[str]:
        tags = [*self.acquires.values(), self.returns]
        return {tag.condition for tag in tags if tag is not None and tag.condition}


def contract_of(function: Node, annotations: Annotations) -> Contract:
    declaration = describe_declaration(function)
    spec = annotations.get(declaration.name) if declaration is not None else None
    if spec is None:
        return Contract({}, {})
    params = declaration.params
    targets: dict[str, dict[str, Tag]] = {"acquires": {}, "releases": {}}
    returns = None
    for tag in spec.tags:
        if tag.target == RETURN_TARGET:
            returns = tag
            continue
        index = spec.arg_index(tag.target)
        if index is not None and index < len(params):
            targets[tag.kind][params[index]] = tag
    return Contract(targets["acquires"], targets["releases"], returns)


def hand_off_events(contract: Contract, events_of: EventsOf, node: Node) -> list[Event]:
    """Releases that give a held identifier to the caller, each carrying the condition of the target's tag."""
    if node.type == "call_expression":
        return [
            Event("release", event.identifier, tag.resource_class, node, tag.condition)
            for event in events_of(node)
            if event.kind == "acquire"
            and (tag := contract.acquires.get(event.identifier)) is not None
        ]
    if is_assignment(node):
        target = dereferenced_root(node.child_by_field_name("left"))
        tag = contract.acquires.get(text(target)) if target is not None else None
        value = uncast_identifier(node.child_by_field_name("right"))
    elif node.type == "return_statement":
        tag = contract.returns
        value = uncast_identifier(
            node.named_children[0] if node.named_children else None
        )
    else:
        return []
    if tag is None or value is None:
        return []
    return [Event("release", text(value), tag.resource_class, node, tag.condition)]


def passes_through(
    contract: Contract, callee_returns: list[Tag], call_node: Node
) -> bool:
    """Whether `return f(...)` hands f's acquire to the caller, under the same `return` tag as the function's own."""
    return callee_returns == [contract.returns] and is_returned(call_node)


def failed_condition(return_node: Node | None) -> str | None:
    """The condition whose failure the statement returns as a literal: `-NAME` or a negative number, or NULL."""
    if return_node is None or return_node.type != "return_statement":
        return None
    value = strip_parentheses(
        return_node.named_children[0] if return_node.named_children else None
    )
    if value is None:
        return None
    if value.type == "number_literal" and text(value).startswith("-"):
        return "on-success"
    if (
        value.type == "unary_expression"
        and text(value.child_by_field_name("operator")) == "-"
        and strip_parentheses(value.child_by_field_name("argument")).type
        in ("identifier", "number_literal")
    ):
        return "on-success"
    if text(value) == "NULL":
        return "on-non-null"
    return None
