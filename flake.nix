{
  description = "UwuChat flake";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
  };

  outputs = {
    self,
    nixpkgs,
  }: let
    # System types to support.
    supportedSystems = ["x86_64-linux" "x86_64-darwin" "aarch64-linux" "aarch64-darwin"];

    # Helper function to generate an attrset '{ x86_64-linux = f "x86_64-linux"; ... }'.
    forAllSystems = nixpkgs.lib.genAttrs supportedSystems;

    # Nixpkgs instantiated for supported system types.
    nixpkgsFor = forAllSystems (system: import nixpkgs {inherit system;});

  in {
    devShells = forAllSystems (system: let
      pkgs = nixpkgsFor.${system};

      raylibPkgs =
        if system == "x86_64-linux"
        then [
          pkgs.xorg.libX11
          pkgs.xorg.libXinerama
          pkgs.libGL
          pkgs.xorg.libXcursor
          pkgs.xorg.libXext
          pkgs.xorg.libXfixes
          pkgs.xorg.libXi
          pkgs.xorg.libXrandr
          pkgs.xorg.libXrender
          pkgs.wayland-scanner
          pkgs.libxkbcommon
          pkgs.wayland
        ]
        else [];
    in {
      default = pkgs.mkShell {
        packages =
          [
            pkgs.zig_0_14
            pkgs.openssl
            pkgs.websocat
            pkgs.entr
            pkgs.gf
            pkgs.go-task
            pkgs.valgrind
            pkgs.process-compose
          ]
          ++ raylibPkgs;

        shellHook = ''
          UWU_LIB_PATH=${pkgs.lib.makeLibraryPath raylibPkgs}
          alias server_dev="find src -type f -iname *.c | entr -r zig build run -- -b 127.0.0.1 -p 8080"
        '';
      };
    });
  };
}
