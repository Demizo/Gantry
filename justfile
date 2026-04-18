board := "nrf52840dk/nrf52840"

default:
  @just --list

# Initialize the west workspace
init:
  west init -l app
  west update

# Update the west workspace
update-workspace:
  west update

alias b := build
# Build the firmware
build:
  west build -b {{board}} app

alias p := rebuild
# Rebuild the firmware (pristine build)
rebuild: 
  west build -p -b {{board}} app
 
alias f := flash
# Flash the firmware
flash:
  west flash --runner openocd

alias e := erase
# Erase the firmware
erase:
  openocd -f app/openocd.cfg -c "init; halt; nrf5 mass_erase; exit"

# Recover the board
recover:
  openocd -f app/openocd.cfg -c "init; nrf52_recover; exit"

# Generate the datastore
gen-datastore:
  uv run tools/datastore/generate_datastore.py --yaml app/datastore.yaml --output-dir app
  just format

# Run static analysis
analyze:
  uv run tools/memory_analysis/generate_leak_check.py 
  spatch --sp-file tools/memory_analysis/generated_leak_check.cocci --dir app/src/ --no-includes --very-quiet
  cppcheck --quiet --inline-suppr --enable=warning,performance,portability --check-level=exhaustive --error-exitcode=1 --inconclusive app/src/

# Format all files
format:
  find app -iname "*.c" -o -iname "*.h" | xargs clang-format -i 

# Build a test suite
build-test component:
  west build -p -b native_sim -d app/tests/build app/tests/{{component}} -- -DCONF_FILE='prj.conf;../test_prj.conf'

# Run all unit tests for the project
test-all:
    west twister -T app/tests/ -p native_sim --clobber-output -i -v

# Run tests for a specific component (e.g., 'just test memory')
test component:
    west twister -T app/tests/{{component}} -p native_sim --clobber-output -i -vv

# Generate documentation
docs:
  cd app && doxygen Doxyfile

# Open documentation
docs-open:
  xdg-open app/docs/html/index.html
