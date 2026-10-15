{
  description = "Pane — a paint.net-style image editor for Linux";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
      buildInputs = with pkgs; [
        qt6.qtbase
        qt6.qtwayland
        qt6.qtsvg
        libpng
        libjpeg_turbo
        zlib
        lz4
        catch2_3
        libwebp
        libavif
        libheif
        libde265
        x265
        libtiff
        giflib
      ];
      nativeBuildInputs = with pkgs; [
        cmake
        ninja
        pkg-config
        qt6.wrapQtAppsHook
      ];
    in
    {
      packages.${system}.default = pkgs.stdenv.mkDerivation {
        pname = "pane";
        version = "0.1.0";
        src = ./.;
        inherit buildInputs nativeBuildInputs;
        cmakeFlags = [ "-DCMAKE_BUILD_TYPE=Release" ];
        doCheck = true;
        checkPhase = "QT_QPA_PLATFORM=offscreen ctest --output-on-failure";
      };

      checks.${system}.default = self.packages.${system}.default;

      devShells.${system}.default = pkgs.mkShell {
        inherit buildInputs;
        nativeBuildInputs = nativeBuildInputs ++ (with pkgs; [ clang-tools gdb ]);
        shellHook = ''
          export QT_PLUGIN_PATH=${pkgs.qt6.qtbase}/${pkgs.qt6.qtbase.qtPluginPrefix}:${pkgs.qt6.qtwayland}/${pkgs.qt6.qtbase.qtPluginPrefix}:${pkgs.qt6.qtsvg}/${pkgs.qt6.qtbase.qtPluginPrefix}
        '';
      };
    };
}
