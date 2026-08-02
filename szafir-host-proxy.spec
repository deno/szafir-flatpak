Name:           szafir-host-proxy
Version:        0.5.3
Release:        1%{?dist}
Summary:        Browser bridge for Szafir website signing

License:        GPL-2.0-only
URL:            https://github.com/deno/szafir-flatpak
Source0:        szafir-host-proxy-%{version}-source.tar.gz

BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  kf6-kcoreaddons-devel
BuildRequires:  kf6-kconfig-devel
BuildRequires:  kf6-kdbusaddons-devel
BuildRequires:  kf6-ki18n-devel
BuildRequires:  kf6-kirigami-devel
BuildRequires:  kf6-kstatusnotifieritem-devel
BuildRequires:  python3
BuildRequires:  python3-pyyaml
BuildRequires:  pkgconf-pkg-config
BuildRequires:  pcsc-lite-devel
BuildRequires:  gnutls-devel
BuildRequires:  p11-kit-devel

Requires:       bubblewrap
# Capability deps (preferred over package names): java-headless resolves to the
# default JDK >= 21 on any Fedora release; the soname covers the PC/SC library
# the JVM dlopens at runtime (resolves to pcsc-lite-libs).
Requires:       java-headless >= 1:21
Requires:       libpcsclite.so.1()(64bit)

%description
SzafirHostProxy is an open-source Native Messaging bridge for Szafir on Linux.
It connects supported browsers with the Szafir signing environment so qualified
signatures can work on supported websites. The bridge downloads required
upstream runtime components during first-run setup instead of bundling them.

%prep
%autosetup -n szafir-host-proxy-%{version}-source

%build
%cmake \
    -DAPP_ID=pl.deno.kir.szafirhostproxy \
    -DAPP_VERSION=%{version} \
    -DRUNTIME_PREFIX=%{_prefix} \
    -DSZAFIR_BWRAP=ON \
    -DSZAFIR_BWRAP_PATH=%{_bindir}/bwrap \
    -DENABLE_FLATPAK_HOST_ICONS_LOOKUP=OFF
%cmake_build

%install
%cmake_install

# CMake installs the license under an app-id dir; drop it in favor of %%license.
rm -rf %{buildroot}%{_datadir}/licenses/pl.deno.kir.szafirhostproxy

# Assets not covered by the CMake install rules.
install -D -p -m 0644 pl.deno.kir.szafirhostproxy.metainfo.xml \
    %{buildroot}%{_datadir}/metainfo/pl.deno.kir.szafirhostproxy.metainfo.xml
install -D -p -m 0644 proxy_icon.svg \
    %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/pl.deno.kir.szafirhostproxy.svg

%files
%license LICENSE
%{_bindir}/szafir-host-proxy
%{_bindir}/szafir-pkcs11-probe
%{_bindir}/start-szafir-host-native.sh
%{_datadir}/szafir-host-proxy/
%{_datadir}/dbus-1/services/pl.deno.kir.szafirhostproxy.service
%{_datadir}/applications/pl.deno.kir.szafirhostproxy.desktop
%{_datadir}/metainfo/pl.deno.kir.szafirhostproxy.metainfo.xml
%{_datadir}/icons/hicolor/scalable/apps/pl.deno.kir.szafirhostproxy.svg
%{_datadir}/locale/

%changelog
* Sun Aug 02 2026 deno <deno@users.noreply.github.com> - 0.5.3-1
- Improved Polish localization with a reproducible translation catalog, placeholder checks, and CI validation. Refreshed AppStream metadata and verified translation packaging.

* Sat Aug 01 2026 deno <deno@users.noreply.github.com> - 0.5.2-1
- Cosmetic fixes.

* Sat Aug 01 2026 deno <deno@users.noreply.github.com> - 0.5.1-1
- Documentation and packaging improvements.

* Sat Aug 01 2026 deno <deno@users.noreply.github.com> - 0.5.0-1
- Added a dark mode.

* Sat Aug 01 2026 deno <deno@users.noreply.github.com> - 0.4.0-1
- The proxy can now see your smart card. The status page shows whether a card is inserted, updating live as you plug and remove it. A new details page lists your card's readers, tokens, and certificates — tap a certificate to inspect it. The app verifies the card provider's integrity before use and re-checks it if the file changes. Also fixes RPM installation on current Fedora and desktop registration so portal prompts (file picker, etc.) work correctly.

* Mon Jul 27 2026 deno <deno@users.noreply.github.com> - 0.3.2-1
- Switch pcsc-lite 2.3.3 download URL to Fedora lookaside mirror; upstream removed older releases from their site, breaking CI builds.

* Mon Jul 27 2026 deno <deno@users.noreply.github.com> - 0.3.1-1
- Revert pcsc-lite to 2.3.3 for Flatpak version for broader compatibility.

* Mon Jul 27 2026 deno <deno@users.noreply.github.com> - 0.3.0-1
- Packaging support now includes Nix and RPM definitions. Bubblewrap is used as a sandboxing mechanism when running outside of Flatpak. SzafirHost runtime and components are automatically updated, eliminating the need for manual version management. The bundled pcsc-lite was upgraded from 2.3.3 to 2.5.1.

* Tue Apr 28 2026 deno <deno@users.noreply.github.com> - 0.2.5-1
- Update to new version of SzafirHost (1.2.2)

* Tue Apr 28 2026 deno <deno@users.noreply.github.com> - 0.2.4-1
- Update to new version of SzafirHost (1.2.1)

* Tue Apr 07 2026 deno <deno@users.noreply.github.com> - 0.2.3-1
- fixed: Landlock launcher sandbox was too restrictive

* Sun Apr 05 2026 deno <deno@users.noreply.github.com> - 0.2.2-1
- Added animations to the status page.

* Sat Apr 04 2026 deno <deno@users.noreply.github.com> - 0.2.1-1
- Bug fixes and README updates.

* Wed Apr 01 2026 deno <deno@users.noreply.github.com> - 0.2.0-1
- Metadata refresh for the GitHub-hosted Flatpak repository release.

