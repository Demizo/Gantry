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

# Run static analysis
analyze:
  uv run tools/memory_analysis/generate_leak_check.py 
  spatch --sp-file tools/memory_analysis/generated_leak_check.cocci --dir app/src/ --no-includes --very-quiet
  cppcheck --quiet --enable=warning,performance,portability --check-level=exhaustive --error-exitcode=1 --inconclusive app/src/

# Generate documentation
docs:
  cd app && doxygen Doxyfile

# Open documentation
docs-open:
  xdg-open app/docs/html/index.html
