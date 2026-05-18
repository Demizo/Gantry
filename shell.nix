{
  pkgs,
  commonPackages,
}:

pkgs.mkShell {
  hardeningDisable = [ "fortify" ];

  packages = with pkgs; [ vscodium-fhs ] ++ commonPackages;
}
