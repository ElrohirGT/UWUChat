{
  description = "UwuChat flake";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
    nixpkgs-GL.url = "github:nixos/nixpkgs?ref=7cf5ccf1cdb2ba5f08f0ac29fc3d04b0b59a07e4";
  };

  outputs = {
    self,
    nixpkgs,
    nixpkgs-GL
  }: let
    # System types to support.
    supportedSystems = ["x86_64-linux" "x86_64-darwin" "aarch64-linux" "aarch64-darwin"];

    # Helper function to generate an attrset '{ x86_64-linux = f "x86_64-linux"; ... }'.
    forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
    forAllSystemsGL = nixpkgs-GL.lib.genAttrs supportedSystems;

    # Nixpkgs instantiated for supported system types.
    nixpkgsFor = forAllSystems (system: import nixpkgs {inherit system;});
    nixpkgsForGL = forAllSystemsGL (system: import nixpkgs-GL {inherit system;});

  in {
    devShells = forAllSystems (system: let
      pkgs = nixpkgsFor.${system};

      pkgs-GL = nixpkgsForGL.${system};
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
        
      GLXPkgs =
        if system == "x86_64-linux"
        then [
          pkgs-GL.xorg.libX11
          pkgs-GL.xorg.libXinerama
          pkgs-GL.libGL
          pkgs-GL.xorg.libXcursor
          pkgs-GL.xorg.libXext
          pkgs-GL.xorg.libXfixes
          pkgs-GL.xorg.libXi
          pkgs-GL.xorg.libXrandr
          pkgs-GL.xorg.libXrender
          pkgs-GL.wayland-scanner
          pkgs-GL.libxkbcommon
          pkgs-GL.wayland
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
      rayo = pkgs-GL.mkShell {
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
          ++ GLXPkgs;

        shellHook = ''
          UWU_LIB_PATH=${pkgs-GL.lib.makeLibraryPath raylibPkgs}
          alias server_dev="find src -type f -iname *.c | entr -r zig build run -- -b 127.0.0.1 -p 8080"
        '';
      };
    });
  };
}
