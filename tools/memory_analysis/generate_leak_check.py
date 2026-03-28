import yaml
from jinja2 import Environment, FileSystemLoader
from pathlib import Path

def normalize_functions(funcs):
    """Ensure all functions have consistent structure."""
    normalized = []
    for f in funcs:
        normalized.append({
            "name": f["name"],
            "single_param": f["single_param"]
        })
    return normalized


def main():
    script_dir = Path(__file__).parent
    function_spec_path = script_dir / "memory_functions.yaml"
    output_patch_file = script_dir / "generated_leak_check.cocci"

    # Load YAML config
    with open(function_spec_path, "r") as f:
        config = yaml.safe_load(f)

    alloc_functions = normalize_functions(config.get("alloc_functions", []))
    dealloc_functions = normalize_functions(config.get("dealloc_functions", []))

    # Load Jinja template
    env = Environment(
        loader=FileSystemLoader(script_dir),
        trim_blocks=True,
        lstrip_blocks=True
    )

    template = env.get_template("generated_leak_check.cocci.j2")

    output = template.render(
        alloc_functions=alloc_functions,
        dealloc_functions=dealloc_functions
    )

    # Write output spatch file
    with open(output_patch_file, "w") as f:
        f.write(output)


if __name__ == "__main__":
    main()