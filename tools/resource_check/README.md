# Resource Checker

Reports a resource that is acquired in a function and not released on every path to a `return`. It reads C syntax via [tree-sitter](https://tree-sitter.github.io/tree-sitter/). What counts as an acquire or a release is declared in doc comment tags:

```c
/** @acquires memory block_ptr on-success */
int mem_alloc(size_t size, void** block_ptr);

/** @releases memory block_ptr */
void mem_unref(void** block_ptr);
```

Annotations for common Zephyr APIs are bundled.

## Install

```sh
uv add --dev "resource-check @ git+https://github.com/Demizo/Gantry#subdirectory=tools/resource_check"
```

## Use

```sh
uv run resource-check <path>... [--annotations <file-or-dir>]...
```

The tag syntax and the checker's limits are described in the
[Gantry documentation](https://github.com/Demizo/Gantry/blob/main/docs/source/resource_checker/index.rst).
