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

flash:
  west flash --runner openocd

