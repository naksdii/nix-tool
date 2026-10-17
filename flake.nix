{
  description = "nix-tool";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in
    {
      packages.${system}.default = pkgs.stdenv.mkDerivation {
        pname = "nix-tool";
        version = "0.1.0";
        src = ./.;

        nativeBuildInputs = with pkgs; [
          gcc
          gnumake
          bash
        ];

        buildPhase = ''
          chmod +x build.sh
          bash ./build.sh
        '';
installPhase = ''
          mkdir -p $out/bin
          cp nix-tool $out/bin/
        '';
      };

      devShells.${system}.default = pkgs.mkShell {
        packages = with pkgs; [
          gcc
          gnumake
          clang-tools
        ];
      };
    };
}