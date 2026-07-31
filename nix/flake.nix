{
  description = "AnLand KWin producer for the DroidSpaces NixOS 25.11 image";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/3cbe716e2346710d6e1f7c559363d14e11c32a43";

  outputs = { nixpkgs, ... }:
    let
      system = "aarch64-linux";
      pkgs = import nixpkgs { inherit system; };
      lib = pkgs.lib;

      kwinAnland = pkgs.kdePackages.kwin.overrideAttrs (old: {
        pname = "kwin-anland";
        postPatch = lib.optionalString (old ? postPatch && old.postPatch != null) old.postPatch + ''
          if patch --dry-run -p1 < ${../producers/kde/ubuntu2604_v5/kwin.patch} >/dev/null 2>&1; then
            patch -p1 < ${../producers/kde/ubuntu2604_v5/kwin.patch}
            cp -r ${../producers/kde/anland_backend_v5/src/backends/anland} src/backends/anland
            echo ubuntu2604-v5 > anland-patch-variant
          elif patch --dry-run -p1 < ${../producers/kde/Debian13_v5/kwin.patch} >/dev/null 2>&1; then
            patch -p1 < ${../producers/kde/Debian13_v5/kwin.patch}
            cp -r ${../producers/kde/anland_backend_debian13_v5/src/backends/anland} src/backends/anland
            echo debian13-v5 > anland-patch-variant
          else
            echo "No AnLand KWin patch matches KWin ${old.version}" >&2
            exit 1
          fi
        '';
      });

      xwaylandAnland = pkgs.xwayland.overrideAttrs (old: {
        pname = "xwayland-anland";
        postPatch = lib.optionalString (old ? postPatch && old.postPatch != null) old.postPatch + ''
          if patch --dry-run -p1 < ${../producers/kde/ubuntu2604_v5/xwayland.patch} >/dev/null 2>&1; then
            patch -p1 < ${../producers/kde/ubuntu2604_v5/xwayland.patch}
          elif patch --dry-run -p1 < ${../producers/kde/Debian13_v5/xwayland.patch} >/dev/null 2>&1; then
            patch -p1 < ${../producers/kde/Debian13_v5/xwayland.patch}
          else
            echo "AnLand Xwayland patch did not match; keeping stock Xwayland" >&2
          fi
        '';
      });

      anlandDesktop = pkgs.buildEnv {
        name = "anland-plasma-desktop";
        ignoreCollisions = true;
        paths = [
          kwinAnland
          xwaylandAnland
          pkgs.kdePackages.plasma-desktop
          pkgs.kdePackages.plasma-workspace
          pkgs.kdePackages.systemsettings
          pkgs.kdePackages.konsole
          pkgs.kdePackages.dolphin
          pkgs.dbus
          pkgs.xorg.xauth
        ];
        pathsToLink = [ "/bin" "/lib" "/share" ];
      };
    in {
      packages.${system} = {
        kwin-anland = kwinAnland;
        xwayland-anland = xwaylandAnland;
        anland-desktop = anlandDesktop;
        default = anlandDesktop;
      };
    };
}
