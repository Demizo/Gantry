{
  pkgs,
  packages,
}:

pkgs.mkShell {
  hardeningDisable = [ "fortify" ];

  inherit packages;

  # The interpreter already carries its packages; PYTHONPATH would shadow the ones in uv's virtual environments
  shellHook = "unset PYTHONPATH";
}
