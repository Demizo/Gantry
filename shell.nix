{
  pkgs,
  zephyr,
}:

pkgs.mkShell {
  packages = with pkgs; [
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
    just-lsp
    mcuboot-imgtool
    doxygen
    uv
    cppcheck
    coccinelle
    clang-tools
    vscodium-fhs
  ];
}
