{
  description = "SzafirHost Proxy - browser native messaging bridge for Szafir e-signature";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
      version = "0.2.5";
    in {
      packages.${system}.default = pkgs.stdenv.mkDerivation {
        pname = "szafir-host-proxy";
        inherit version;
        src = ./.;

        nativeBuildInputs = [
          pkgs.cmake
          pkgs.ninja
          pkgs.python3
          pkgs.python3Packages.pyyaml
          pkgs.kdePackages.wrapQtAppsHook
        ];

        buildInputs = [
          pkgs.qt6.qtbase
          pkgs.qt6.qtdeclarative
          pkgs.kdePackages.kcoreaddons
          pkgs.kdePackages.kconfig
          pkgs.kdePackages.kdbusaddons
          pkgs.kdePackages.ki18n
          pkgs.kdePackages.kirigami
          pkgs.kdePackages.kstatusnotifieritem
        ];

        cmakeDir = "../szafir-host-proxy";

        cmakeFlags = [
          "-DAPP_VERSION=${version}"
          "-DENABLE_FLATPAK_HOST_ICONS_LOOKUP=OFF"
          "-DRUNTIME_PREFIX=${placeholder "out"}"
        ];

        preConfigure = ''
          python3 scripts/generate_permissions_header.py \
            --runtime-prefix "$out" \
            szafir-host-proxy/permissions.yml szafir-host-proxy/generated_permissions.h
        '';

        postInstall = ''
          ln -s ${pkgs.jdk21}/lib/openjdk $out/jre
        '';
      };

      devShells.${system}.default = pkgs.mkShell {
        inputsFrom = [ self.packages.${system}.default ];
      };
    };
}
