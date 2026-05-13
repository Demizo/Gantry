board := "nrf52840dk/nrf52840"
sample := "samples/example_app"

default:
  @just --list

# Initialize the west workspace
init:
  west init -l .
  west update

# Update the west workspace
update-workspace:
  west update

alias b := build
# Build the sample application
build:
  west build -b {{board}} {{sample}} -d build

alias p := rebuild
# Pristine rebuild of the sample application
rebuild:
  west build -p -b {{board}} {{sample}} -d build

alias f := flash
# Flash the sample
flash:
  west flash --runner openocd

alias e := erase
# Mass-erase the board
erase:
  openocd -f {{sample}}/openocd.cfg -c "init; halt; nrf5 mass_erase; mww 0x4001e504 1; mww 0x10001208 0x5a; mww 0x4001e504 0; exit"

# Recover the board
recover:
  openocd -f {{sample}}/openocd.cfg -c "init; nrf52_recover; exit"

# View RTT logs
rtt:
  uv run tools/rtt_logger.py --device {{board}} --config {{sample}}/openocd.cfg

# Static + resource analysis on library and sample sources
analyze:
  uv run tools/check_headers.py
  uv run tools/resource_check/generate_resource_check.py
  -spatch --sp-file tools/resource_check/generated_resource_check.cocci --dir lib/ --no-includes --very-quiet
  -spatch --sp-file tools/resource_check/generated_resource_check.cocci --dir {{sample}}/src/ --no-includes --very-quiet
  cppcheck --quiet --inline-suppr --enable=warning,performance,portability --check-level=exhaustive --error-exitcode=1 --inconclusive lib/ {{sample}}/src/

# Format the library, sample, and test sources
format:
  find lib include {{sample}} tests -path "*/build" -prune -o \( -iname "*.c" -o -iname "*.h" \) -print | xargs clang-format -i

# Build a single test suite
build-test component:
  west build -p -b native_sim -d tests/build tests/{{component}} -- -DCONF_FILE='prj.conf;../test_prj.conf'

# Run all unit tests
test-all:
  west twister -T tests/ -p native_sim --clobber-output -i -v

# Run tests for a specific component (e.g. `just test memory`)
test component:
  west twister -T tests/{{component}} -p native_sim --clobber-output -i -vv

# Build documentation
docs:
  doxygen Doxyfile
  cd docs && uv run make html

# Open documentation in browser
docs-open:
  xdg-open docs/build/html/index.html

# Open doxygen documents
doxy-open:
  xdg-open docs/doxygen/html/index.html
