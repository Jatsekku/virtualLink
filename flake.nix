{
  description = "Development flake for logger library";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    logger.url = "github:Jatsekku/logger";
  };

  outputs =
    inputs@{ flake-parts, ... }:
    flake-parts.lib.mkFlake { inherit inputs; } {
      systems = [
        "x86_64-linux"
      ];
      perSystem =
        {
          config,
          self',
          inputs',
          pkgs,
          system,
          ...
        }:
        {
          # Set formatter for nix fmt
          formatter = pkgs.nixfmt-rfc-style;

          packages.virtualLink = pkgs.callPackage ./package.nix { inherit logger; };
          packages.default = config.packages.virtualLink;

          devShells.default = config.packages.default;
        };
    };
}
