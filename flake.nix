{
  description = "Gantry Development Environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";

    # west.yml's Zephyr revision, whose requirements zephyr-nix builds the Python environment from
    zephyr.url = "github:zephyrproject-rtos/zephyr/v4.4.0";
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

        sdk = zephyr.sdk.override {
          targets = [
            "arm-zephyr-eabi"
            "x86_64-zephyr-elf"
          ];
        };

        corePackages = with pkgs; [
          sdk
          zephyr.pythonEnv
          zephyr.hosttools-nix
          cmake
          ninja
          just
          mcuboot-imgtool
          uv
          cppcheck
          clang-tools
          doxygen
        ];

        extraPackages = with pkgs; [
          nrfutil
          gdb
          just-lsp
        ];
      in
      {
        devShells.default = import ./shell.nix {
          inherit pkgs;
          packages = corePackages ++ extraPackages ++ [ pkgs.vscodium-fhs ];
        };

        devShells.ci = import ./shell.nix {
          inherit pkgs;
          packages = corePackages;
        };

        packages.docker = pkgs.dockerTools.buildLayeredImage {
          name = "gantry-devenv";
          tag = "latest";
          contents =
            with pkgs;
            stdenv.initialPath
            ++ [
              bashInteractive
              git
              cacert
              dockerTools.usrBinEnv
            ]
            ++ corePackages
            ++ extraPackages
            ++ zephyr.hosttools-nix.propagatedBuildInputs;
          extraCommands = "mkdir -m 1777 tmp";
          config = {
            Env = [
              "HOME=/tmp"
              "UV_LINK_MODE=copy"
              "SSL_CERT_FILE=${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt"
              "ZEPHYR_SDK_INSTALL_DIR=${sdk}"
            ];
          };
        };
      }
    ));
}
