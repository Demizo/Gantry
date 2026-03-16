board := "nrf52840dk/nrf52840"

default:
  @just --list

init:
  west init -l app
  west update

build:
  west build -b {{board}} app

rebuild:
  west build -p -b {{board}} app
 
mem_leak:
  spatch --sp-file memory_leak_check.cocci --dir app/src/ --no-includes --very-quiet

flash:
  west flash --runner openocd

docs:
  cd app && doxygen Doxyfile

docs-open:
  xdg-open app/docs/html/index.html
