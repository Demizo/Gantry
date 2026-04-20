{
  pkgs,
  zephyr,
}:

pkgs.mkShell {
  hardeningDisable = [ "fortify" ];
  packages = with pkgs; [
    (zephyr.sdk.override {
      targets = [
        "arm-zephyr-eabi"
        "x86_64-zephyr-elf"
      ];
    })
    zephyr.pythonEnv
    zephyr.hosttools-nix
    cmake
    ninja
    nrfutil
    just
    just-lsp
    mcuboot-imgtool
    doxygen
    uv
    cppcheck
    coccinelle
    clang-tools
    vscodium-fhs
    gdb
  ];
}
