from tree_sitter import Node


def text(node: Node | None) -> str:
    return node.text.decode("utf-8") if node is not None else ""


def line(node: Node) -> int:
    return node.start_point[0] + 1


def find_calls(node: Node | None) -> list[Node]:
    if node is None:
        return []
    calls: list[Node] = []

    def walk(n: Node):
        if n.type == "call_expression":
            calls.append(n)
        for child in n.children:
            walk(child)

    walk(node)
    return calls


def call_name(call_node: Node) -> str | None:
    callee = call_node.child_by_field_name("function")
    return text(callee) if callee is not None and callee.type == "identifier" else None


def call_args(call_node: Node) -> list[Node]:
    arg_list = call_node.child_by_field_name("arguments")
    return list(arg_list.named_children) if arg_list is not None else []


def unwrap_identifier_reference(node: Node | None) -> Node | None:
    while node is not None:
        if node.type == "identifier":
            return node
        if node.type == "pointer_expression":
            operator = node.child_by_field_name("operator")
            if operator is None or operator.text != b"&":
                return None
            node = node.child_by_field_name("argument")
        elif node.type == "cast_expression":
            node = node.child_by_field_name("value")
        else:
            return None
    return None


def iter_nodes(node: Node):
    stack = [node]
    while stack:
        current = stack.pop()
        yield current
        stack.extend(reversed(current.children))


def declarator_identifier(declarator: Node | None) -> Node | None:
    while declarator is not None and declarator.type != "identifier":
        inner = declarator.child_by_field_name("declarator")
        if inner is None:
            inner = declarator.named_children[0] if declarator.named_children else None
        declarator = inner
    return declarator


def result_identifier(call_node: Node) -> Node | None:
    parent = call_node.parent
    if parent is None:
        return None
    if (
        parent.type == "init_declarator"
        and parent.child_by_field_name("value") == call_node
    ):
        return declarator_identifier(parent.child_by_field_name("declarator"))
    if (
        parent.type == "assignment_expression"
        and parent.child_by_field_name("right") == call_node
    ):
        left = parent.child_by_field_name("left")
        if (
            left is not None
            and left.type == "identifier"
            and text(parent.child_by_field_name("operator")) == "="
        ):
            return left
    return None


def contains_label(node: Node) -> bool:
    return node.type == "labeled_statement" or any(
        contains_label(child) for child in node.children
    )


def strip_parentheses(node: Node | None) -> Node | None:
    while (
        node is not None
        and node.type == "parenthesized_expression"
        and node.named_children
    ):
        node = node.named_children[0]
    return node
