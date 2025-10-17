{
  description = "Development shell for rofi with daemon support work";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        baseTools = with pkgs; [
          meson
          ninja
          pkg-config
          bison
          flex
          git
          which
          gnumake
          nodejs
          gdb
        ];
        coreLibs = with pkgs; [
          glib
          gdk-pixbuf
          cairo
          pango
          librsvg
          libstartup_notification
          libxkbcommon
          check
        ];
        xorg = pkgs.xorg;
        x11Libs =
          [
            xorg.libxcb
            xorg.xcbutil
            xorg.xcbutilwm
            xorg.xcbutilkeysyms
            xorg.xcbutilrenderutil
            pkgs.xcb-imdkit
            pkgs.xcbutilxrm
            pkgs."xcb-util-cursor"
          ];
        waylandLibs = with pkgs; [
          wayland
          wayland-protocols
          wayland-scanner
        ];
      in {
        devShells.default = pkgs.mkShell {
          packages = baseTools ++ coreLibs ++ x11Libs ++ waylandLibs;
        };
      });
}
