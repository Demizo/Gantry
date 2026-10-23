"""Renderings through the package's Jinja templates."""

from collections.abc import Callable

import jinja2


def render(template: str, filters: dict[str, Callable], **context) -> str:
    environment = jinja2.Environment(
        loader=jinja2.PackageLoader("protocol_spec"),
        trim_blocks=True,
        lstrip_blocks=True,
        keep_trailing_newline=True,
        undefined=jinja2.StrictUndefined,
    )
    environment.filters.update(filters)
    return environment.get_template(template).render(**context)
