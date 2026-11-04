board := "nrf52840dk/nrf52840"
# Each supported board, then its chip as `probe-rs chip list` names it
boards := '''
  nrf52840dk/nrf52840                     nRF52840_xxAA
  nrf54l15dk/nrf54l15/cpuapp              nRF54L15
  nrf54lm20dk/nrf54lm20a/cpuapp           nRF54LM20A
'''
chip := if boards =~ '(?m)^' + board + ' ' {
  replace_regex(boards, '(?ms).*^' + board + ' +(\S+).*', '$1')
} else {
  error('unknown board ' + board + ", choose one of:\n" + boards)
}
board_platforms := trim(replace_regex(boards, '(?m)^(\S+).*\n', '-p $1 '))
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

# Enter or run a command inside the dev container, with this checkout's west workspace mounted at its own path
[group('setup')]
docker-run *args='bash':
  docker run --rm $([ -t 0 ] && echo --interactive --tty) \
      --user "$(id -u):$(id -g)" \
      -v "$(dirname "$(pwd)"):$(dirname "$(pwd)")" \
      -w "$(pwd)" \
      gantry-devenv:latest {{args}}

alias b := build
# Build the sample application
[group('build')]
build:
  west build -p auto -b {{board}} {{sample}} -d build

alias p := rebuild
# Pristine rebuild of the sample application
[group('build')]
rebuild:
  west build -p -b {{board}} {{sample}} -d build

alias f := flash
# Flash the sample and reset the board
[group('build')]
flash:
  probe-rs download --chip {{chip}} build/zephyr/zephyr.elf
  probe-rs reset --chip {{chip}}

alias e := erase
# Mass-erase the board, keeping debug access open
[group('build')]
erase *flags:
  #!/usr/bin/env bash
  set -euo pipefail
  probe-rs erase --chip {{chip}} {{flags}}
  if [[ {{chip}} == nRF52840_xxAA ]]; then
    # Recent nRF52840 revisions lock debug access at reset unless UICR APPROTECT reads HwDisabled
    probe-rs write --chip {{chip}} b32 0x4001e504 1     # NVMC CONFIG: write enabled
    probe-rs write --chip {{chip}} b32 0x10001208 0x5a  # UICR APPROTECT: HwDisabled
    probe-rs write --chip {{chip}} b32 0x4001e504 0     # NVMC CONFIG: read only
  fi

# Unlock a protected board by erasing everything on it
[group('build')]
recover: (erase "--allow-erase-all")

# View RTT logs, leaving the board running through a reset or fault
[group('build')]
rtt:
  probe-rs attach --chip {{chip}} --no-catch-reset --no-catch-hardfault build/zephyr/zephyr.elf

# Bash for check and test: `step NAME CMD...` runs a command, `summary` prints each step's result and fails if any did
steps := '''
results=() failed=0
step() {
  local name=$1 start=$SECONDS status=ok; shift
  printf '\n=== %s\n' "$name"
  "$@" || { status=FAILED; failed=1; }
  results+=("$(printf '%-22s %-7s %4ss' "$name" "$status" $((SECONDS - start)))")
}
summary() {
  echo
  printf '%s\n' "${results[@]}"
  (( failed )) && echo FAILED || echo passed
  return $failed
}
'''

twister := "west twister --clobber-output --inline-logs"
python_suites := "stow_gen protocol_spec python_client link_broker resource_check"

# Check headers, formatting, lint, leaks, cppcheck and vectors, changing no file
[group('check')]
check:
  #!/usr/bin/env bash
  {{steps}}
  step headers uv run tools/check_headers.py
  step clang-format clang-format --dry-run -Werror $({{c_sources}})
  step ruff uv run ruff check .
  step "ruff format" uv run ruff format --check .
  step resource-check uv run --project tools/resource_check resource-check lib/ {{sample}}/src/ --annotations include --annotations {{sample}}/inc
  step "resource-check peers" uv run --project tools/resource_check resource-check samples/stow_peers/common/src/ samples/stow_peers/controller/src/ samples/stow_peers/sensor/src/ --annotations include --annotations samples/stow_peers/common/inc
  step cppcheck cppcheck {{cppcheck_flags}} lib/ {{sample}}/src/
  for folder in protocols/*/; do step "$folder" {{just_executable()}} protocol_spec check "$folder"; done
  summary

# Apply clang-format, ruff and the copyright header
[group('check')]
fix:
  clang-format -i $({{c_sources}})
  uv run tools/check_headers.py --fix
  uv run ruff check --fix-only .
  uv run ruff format .

# Run test tiers that need no hardware: unit (Python and native_sim suites), network (stow_peers over link_broker), boards (sample builds)
[group('test')]
test *tiers='unit network boards':
  #!/usr/bin/env bash
  {{steps}}
  unit() {
    for suite in {{python_suites}}; do step "$suite" {{just_executable()}} "$suite" test -q; done
    step tests/check_headers uv run pytest -q tests/check_headers
    step tests/resource_check uv run --project tools/resource_check pytest -q tests/resource_check
    step native_sim {{twister}} -T tests -p native_sim -O twister-out-native_sim
  }
  network() { step stow_peers {{just_executable()}} stow_peers test -q; }
  boards() { step boards {{twister}} -T {{sample}} {{board_platforms}} -O twister-out-boards; }
  for tier in {{tiers}}; do
    case $tier in unit|network|boards) ;; *) echo "unknown tier '$tier': choose unit, network or boards" >&2; exit 2 ;; esac
  done
  for tier in {{tiers}}; do "$tier"; done
  summary

# Run one twister suite, such as `just test-suite memory`
[group('test')]
test-suite name:
  {{twister}} -vv -T tests/{{name}} -p native_sim -O twister-out-{{name}}

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
