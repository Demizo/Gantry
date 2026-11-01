board := "nrf52840dk/nrf52840"
sample := "samples/example_app"
c_sources := "find lib include samples tests -path '*/build' -prune -o -name '*.[ch]' -print"
cppcheck_flags := replace_regex(read("config/cppcheck-flags.txt"), '\s+', " ")

[private]
default:
  @just --list --unsorted

# Initialize the west workspace
[group('setup')]
init:
  west init -l .
  west update

# Update the west workspace
[group('setup')]
update:
  west update

# Build the dev container image
[group('setup')]
docker-build:
  nix build .#docker
  docker load < result
  rm result

# Enter or run a command inside the dev container
[group('setup')]
docker-run *args='bash':
  docker run --rm -it \
      --user "$(id -u):$(id -g)" \
      -v "$(pwd):/workspace" \
      -w /workspace \
      gantry-devenv:latest {{args}}

alias b := build
# Build the sample application
[group('build')]
build:
  west build -b {{board}} {{sample}} -d build

alias p := rebuild
# Pristine rebuild of the sample application
[group('build')]
rebuild:
  west build -p -b {{board}} {{sample}} -d build

alias f := flash
# Flash the sample
[group('build')]
flash:
  west flash --runner openocd

alias e := erase
# Mass-erase the board
[group('build')]
erase:
  openocd -f {{sample}}/openocd.cfg -c "init; halt; nrf5 mass_erase; mww 0x4001e504 1; mww 0x10001208 0x5a; mww 0x4001e504 0; exit"

# Recover the board
[group('build')]
recover:
  openocd -f {{sample}}/openocd.cfg -c "init; nrf52_recover; exit"

# View RTT logs
[group('build')]
rtt:
  uv run tools/rtt_logger.py --device {{board}} --config {{sample}}/openocd.cfg

# Check headers, formatting, lint, leaks, cppcheck and vectors, changing no file
[group('check')]
check:
  #!/usr/bin/env bash
  failed=()
  run() { local name=$1; shift; "$@" || failed+=("$name"); }
  run headers uv run tools/check_headers.py
  run clang-format clang-format --dry-run -Werror $({{c_sources}})
  run ruff uv run ruff check .
  run "ruff format" uv run ruff format --check .
  run resource-check uv run --project tools/resource_check resource-check lib/ {{sample}}/src/ --annotations include --annotations {{sample}}/inc
  run cppcheck cppcheck {{cppcheck_flags}} lib/ {{sample}}/src/
  for folder in protocols/*/; do
    run "$folder" {{just_executable()}} protocol_spec check "$folder"
  done
  if (( ${#failed[@]} )); then
    echo "check failed: ${failed[*]}" >&2
    exit 1
  fi

# Apply clang-format, ruff and the copyright header
[group('check')]
fix:
  clang-format -i $({{c_sources}})
  uv run tools/check_headers.py --fix
  uv run ruff check --fix-only .
  uv run ruff format .

# Run every test suite that needs no hardware
[group('test')]
test:
  #!/usr/bin/env bash
  failed=()
  run() { local name=$1; shift; "$@" || failed+=("$name"); }
  run twister west twister -T tests/ -p native_sim --clobber-output -i -v
  run tests/resource_check uv run --project tools/resource_check pytest tests/resource_check
  run tests/check_headers uv run pytest tests/check_headers
  # The example app's tests need a board
  for module in stow_peers python_client protocol_spec stow_gen resource_check link_broker; do
    run "$module" {{just_executable()}} "$module" test
  done
  if (( ${#failed[@]} )); then
    echo "test failed: ${failed[*]}" >&2
    exit 1
  fi

# Run one twister suite, such as `just test-suite memory`
[group('test')]
test-suite name:
  west twister -T tests/{{name}} -p native_sim --clobber-output -i -vv

# Build the documentation
[group('docs')]
docs:
  doxygen Doxyfile
  cd docs && uv run make html

# Open the documentation in a browser
[group('docs')]
docs-open:
  xdg-open docs/build/html/index.html

# Integration tests of the example app on a board
[group('modules')]
mod example_app 'samples/example_app'

# Controller and sensor samples as a simulated network
[group('modules')]
mod stow_peers 'samples/stow_peers'

# Python Stow client
[group('modules')]
mod python_client 'python/stow-client'

# Protocol vectors and wire constants
[group('modules')]
mod protocol_spec 'tools/protocol_spec'

# Stow code generator
[group('modules')]
mod stow_gen 'tools/stow_gen'

# Resource checker
[group('modules')]
mod resource_check 'tools/resource_check'

# Link broker for simulated networks
[group('modules')]
mod link_broker 'tools/link_broker'
