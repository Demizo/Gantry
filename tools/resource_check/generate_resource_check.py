import argparse
import yaml
from jinja2 import Environment, FileSystemLoader
from pathlib import Path


def normalize_functions(funcs):
    return [{"name": f["name"], "single_param": f["single_param"]} for f in funcs]


def load_yaml(path: Path) -> dict:
    with open(path, "r") as f:
        return yaml.safe_load(f) or {}


def main():
    script_dir = Path(__file__).parent

    parser = argparse.ArgumentParser()
    parser.add_argument("--library-yaml", type=Path,
                        default=script_dir / "resource_functions.yaml")
    parser.add_argument("--app-yaml", type=Path, default=None)
    parser.add_argument("--output", type=Path,
                        default=script_dir / "generated_resource_check.cocci")
    args = parser.parse_args()

    lib = load_yaml(args.library_yaml)
    alloc_functions = normalize_functions(lib.get("alloc_functions", []))
    dealloc_functions = normalize_functions(lib.get("dealloc_functions", []))

    if args.app_yaml is not None:
        app = load_yaml(args.app_yaml)
        alloc_functions += normalize_functions(app.get("alloc_functions", []))
        dealloc_functions += normalize_functions(app.get("dealloc_functions", []))

    env = Environment(
        loader=FileSystemLoader(script_dir),
        trim_blocks=True,
        lstrip_blocks=True,
    )
    template = env.get_template("generated_resource_check.cocci.j2")
    output = template.render(
        alloc_functions=alloc_functions,
        dealloc_functions=dealloc_functions,
    )

    with open(args.output, "w") as f:
        f.write(output)


if __name__ == "__main__":
    main()