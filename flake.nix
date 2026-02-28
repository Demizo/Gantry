{
  description = "A project built with west & west2nix";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";

    # Customize the version of Zephyr used by the flake here
    zephyr.url = "github:zephyrproject-rtos/zephyr/v4.3.0";
    zephyr.flake = false;

    flake-utils.url = "github:numtide/flake-utils";

    zephyr-nix.url = "github:adisbladis/zephyr-nix";
    zephyr-nix.inputs.nixpkgs.follows = "nixpkgs";
    zephyr-nix.inputs.zephyr.follows = "zephyr";

    west2nix.url = "github:adisbladis/west2nix";
    west2nix.inputs.nixpkgs.follows = "nixpkgs";
    west2nix.inputs.zephyr-nix.follows = "zephyr-nix";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      ...
    }@inputs:
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

        callPackage = pkgs.newScope (
          pkgs
          // {
            zephyr = inputs.zephyr-nix.packages.${system};
            west2nix = callPackage inputs.west2nix.lib.mkWest2nix { };
          }
        );
      in
      {
        packages.default = callPackage ./default.nix { };
        devShells.default = callPackage ./shell.nix { };
      }
    ));
}
