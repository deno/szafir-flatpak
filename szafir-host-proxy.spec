Name:           szafir-host-proxy
Version:        0.3.2
Release:        1%{?dist}
Summary:        Browser bridge for Szafir website signing

License:        GPL-2.0-only
URL:            https://github.com/deno/szafir-flatpak
Source0:        szafir-host-proxy-0.3.0-source.tar.gz

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
%autosetup -n szafir-host-proxy-0.3.0-source

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
* Mon Jul 27 2026 deno - 0.3.0-1.20260727230709449376.master.0.ga0b846b
- Packaging support now includes Nix and RPM definitions. Bubblewrap is used as a sandboxing mechanism when running outside of Flatpak. SzafirHost runtime and components are automatically updated, eliminating the need for manual version management. The bundled pcsc-lite was upgraded from 2.3.3 to 2.5.1.

* Mon Jul 27 2026 deno <deno@users.noreply.github.com> - 0.3.0-1
- Unified component discovery on the web; dropped the bundled manifest.
