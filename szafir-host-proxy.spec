Name:           szafir-host-proxy
Version:        0.3.0
Release:        1%{?dist}
Summary:        Browser bridge for Szafir website signing

License:        GPL-2.0-only
URL:            https://github.com/deno/szafir-flatpak
Source0:        %{name}-%{version}.tar.gz

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

Requires:       bubblewrap
Requires:       java-21-openjdk-headless
Requires:       pcsclite-libs

%description
SzafirHostProxy is an open-source Native Messaging bridge for Szafir on Linux.
It connects supported browsers with the Szafir signing environment so qualified
signatures can work on supported websites. The bridge downloads required
upstream runtime components during first-run setup instead of bundling them.

%prep
%autosetup -n %{name}-%{version}

# The committed header bakes in the Flatpak /app prefix; regenerate for /usr.
python3 scripts/generate_permissions_header.py \
    --runtime-prefix %{_prefix} \
    szafir-host-proxy/permissions.yml \
    szafir-host-proxy/generated_permissions.h

%build
%set_build_flags
# Build the CMake project in the szafir-host-proxy/ subdirectory directly
# (the %%cmake macro assumes the spec source root is the CMake root).
cmake -S szafir-host-proxy -B build -G Ninja \
    -DCMAKE_C_FLAGS_RELEASE:STRING="-DNDEBUG" \
    -DCMAKE_CXX_FLAGS_RELEASE:STRING="-DNDEBUG" \
    -DCMAKE_VERBOSE_MAKEFILE:BOOL=ON \
    -DCMAKE_INSTALL_DO_STRIP:BOOL=OFF \
    -DCMAKE_INSTALL_PREFIX:PATH=%{_prefix} \
    -DCMAKE_INSTALL_LIBDIR:PATH=%{_libdir} \
    -DAPP_ID=pl.deno.kir.szafirhostproxy \
    -DAPP_VERSION=%{version} \
    -DRUNTIME_PREFIX=%{_prefix} \
    -DSZAFIR_BWRAP=ON \
    -DSZAFIR_BWRAP_PATH=%{_bindir}/bwrap \
    -DENABLE_FLATPAK_HOST_ICONS_LOOKUP=OFF
cmake --build build --verbose %{?_smp_mflags}

%install
DESTDIR=%{buildroot} cmake --install build

# CMake installs the license under an app-id dir; drop it in favor of %%license.
rm -rf %{buildroot}%{_datadir}/licenses/pl.deno.kir.szafirhostproxy

# Assets not covered by the CMake install rules.
install -D -p -m 0644 szafir-host-proxy/pl.deno.kir.szafirhostproxy.metainfo.xml \
    %{buildroot}%{_datadir}/metainfo/pl.deno.kir.szafirhostproxy.metainfo.xml
install -D -p -m 0644 szafir-host-proxy/proxy_icon.svg \
    %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/pl.deno.kir.szafirhostproxy.svg

%files
%license szafir-host-proxy/LICENSE
%{_bindir}/szafir-host-proxy
%{_bindir}/start-szafir-host-native.sh
%{_datadir}/szafir-host-proxy/
%{_datadir}/dbus-1/services/pl.deno.kir.szafirhostproxy.service
%{_datadir}/applications/pl.deno.kir.szafirhostproxy.desktop
%{_datadir}/metainfo/pl.deno.kir.szafirhostproxy.metainfo.xml
%{_datadir}/icons/hicolor/scalable/apps/pl.deno.kir.szafirhostproxy.svg
%{_datadir}/locale/

%changelog
* Mon Jul 27 2026 deno <deno@users.noreply.github.com> - 0.3.0-1
- Unified component discovery on the web; dropped the bundled manifest.
