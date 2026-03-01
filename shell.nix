{
  mkShell,
  zephyr,
  callPackage,
  cmake,
  ninja,
  nrfutil,
  just,
  mcuboot-imgtool,
  doxygen,
  lib,
}:

mkShell {
  packages = [
    (zephyr.sdk.override {
      targets = [
        "arm-zephyr-eabi"
      ];
    })
    zephyr.pythonEnv
    zephyr.hosttools-nix
    cmake
    ninja
    nrfutil
    just
    mcuboot-imgtool
    doxygen
  ];
}
