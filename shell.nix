{
  pkgs,
  commonPackages,
}:

pkgs.mkShell {
  hardeningDisable = [ "fortify" ];

  packages = with pkgs; [ vscodium-fhs ] ++ commonPackages;

  # The interpreter already carries its packages; PYTHONPATH would shadow the ones in uv's virtual environments
  shellHook = "unset PYTHONPATH";
}
