{
  description = "Gantry Development Environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";

    # Customize the version of Zephyr used by the flake here
    zephyr.url = "github:zephyrproject-rtos/zephyr/v4.3.0";
    zephyr.flake = false;

    zephyr-nix = {
      url = "github:adisbladis/zephyr-nix";
      inputs = {
        nixpkgs.follows = "nixpkgs";
        zephyr.follows = "zephyr";
      };
    };
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      zephyr-nix,
      ...
    }:
    (flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config = {
            allowUnfree = true;
            segger-jlink.acceptLicense = true;
            allowBroken = true;
            permittedInsecurePackages = [
              "segger-jlink-qt4-824"
            ];
          };
        };

        zephyr = zephyr-nix.packages.${system};

        commonPackages = with pkgs; [
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
          mcuboot-imgtool
          uv
          cppcheck
          coccinelle
          clang-tools
          gdb
          just-lsp
          doxygen
        ];
      in
      {
        devShells.default = import ./shell.nix { inherit pkgs commonPackages; };

        packages.docker = pkgs.dockerTools.buildLayeredImage {
          name = "gantry-devenv";
          tag = "latest";
          contents =
            with pkgs;
            [
              bashInteractive
              coreutils
            ]
            ++ commonPackages;
          config = {
            WorkingDir = "/workspace";
          };
        };
      }
    ));
}
