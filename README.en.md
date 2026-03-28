# szafir-flatpak

<div align="center">
  <p><strong>Community Flatpak packaging for KIR's Szafir electronic signature suite.</strong></p>
  <p>Desktop signing app, browser integration bridge, and Flatpak-friendly native host support in one repository.</p>
  <p>
    <a href="#screenshots">Screenshots</a> |
    <a href="#quick-start">Quick Start</a> |
    <a href="#browser-signing-stack">Browser Signing</a>
  </p>
</div>

> [!IMPORTANT]
> This is a community-maintained package and issue tracker for the Flatpak builds. It is not officially supported by KIR.

## Screenshots

<table>
  <tr>
    <td width="50%" valign="top">
      <img width="100%" src="https://raw.githubusercontent.com/deno/flathub/516be0b374b15331e2183221f539895c0076f0db/screenshots/en/home.png" alt="Szafir home screen" />
    </td>
    <td width="50%" valign="top">
      <img width="100%" src="https://raw.githubusercontent.com/deno/flathub/516be0b374b15331e2183221f539895c0076f0db/screenshots/en/signing_pades.png" alt="Szafir PAdES signing workflow" />
    </td>
  </tr>
  <tr>
    <td align="center"><strong>Standalone desktop app</strong><br />Create and verify qualified electronic signatures inside the Flatpak-packaged Szafir client.</td>
    <td align="center"><strong>Document signing flow</strong><br />The core signing workflow stays fully available in the sandboxed desktop application.</td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <img width="100%" src="https://raw.githubusercontent.com/deno/flathub/67e5b61c60a8e0788c552b73af4c60df8eead385/screenshots/sdk_permission.png" alt="SzafirHostProxy browser permission prompt" />
    </td>
    <td width="50%" valign="top">
      <img width="100%" src="https://raw.githubusercontent.com/deno/flathub/67e5b61c60a8e0788c552b73af4c60df8eead385/screenshots/sdk_pin.png" alt="SzafirHostProxy PIN prompt" />
    </td>
  </tr>
  <tr>
    <td align="center"><strong>Browser permission bridge</strong><br />SzafirHostProxy wires supported browsers into the signing stack, including Flatpaked browsers.</td>
    <td align="center"><strong>Browser-based signing</strong><br />Web signing flows can still reach the secure host process when the required components are installed.</td>
  </tr>
</table>

## Quick Start

Install the Flatpak remote once, then choose the packages you need:

```bash
flatpak remote-add --if-not-exists --from szafir https://deno.github.io/szafir-flatpak/szafir.flatpakrepo
```

```bash
flatpak install szafir pl.kir.szafir
flatpak install szafir pl.kir.szafirhost
flatpak install szafir pl.deno.kir.szafirhostproxy
```

> [!TIP]
> If you only need the standalone desktop app, install `pl.kir.szafir`. For browser-based signing, install the last two packages.

Apps update with:

```bash
flatpak update
```

## Package Overview

| Package | Description |
| --- | --- |
| `pl.kir.szafir` | Application for verifying and creating electronic signatures |
| `pl.kir.szafirhost` | Minimal wrapper for SzafirHost with restricted permissions. Invoked by the `pl.deno.kir.szafirhostproxy` helper |
| `pl.deno.kir.szafirhostproxy` | Automatic browser integration, including browsers installed as Flatpaks. Invokes `pl.kir.szafirhost` and exposes it to the browser. |

## Standalone App

`pl.kir.szafir` is an application for verifying and creating electronic signatures published by Krajowa Izba Rozliczeniowa (KIR).

> [!NOTE]
> On first use, open the signing profile settings, disable the default technical component configuration, and add the Graphite PKCS#11 library from `/app/extra/libCCGraphiteP11.2.0.5.6.so`.

> [!TIP]
> The app can access the Documents folder by default. Use Flatseal if you need to sign files from additional locations.

## Browser Signing Stack

To use electronic signatures directly from web browsers, install the host components and the official KIR browser extension.

### How it fits together

| Component | What it does | Notes |
| --- | --- | --- |
| `pl.kir.szafirhost` | Provides the actual host-side signing runtime | On its own, it requires manual native host setup and does not support Flatpaked browsers well |
| `pl.deno.kir.szafirhostproxy` | Handles browser integration and native messaging manifests | Autostarts on demand, supports host-installed and Flatpaked browsers, and exposes tray-based scaling controls |

To remove the browser integration later:

```bash
flatpak run pl.deno.kir.szafirhostproxy --uninstall
```

### Required browser extensions

- Chrome and Chromium: [Szafir SDK Web on Chrome Web Store](https://chromewebstore.google.com/detail/szafir-sdk-web/gjalhnomhafafofonpdihihjnbafkipc)
- Firefox: [Szafir SDK Web for Firefox (.xpi)](https://www.elektronicznypodpis.pl/download/webmodule/firefox/szafir_sdk_web-current.xpi)

### Supported browsers

- Mozilla Firefox
- LibreWolf
- Waterfox
- Google Chrome
- Google Chrome Dev
- Chromium
- Ungoogled Chromium

## Security Model

SzafirHostProxy does not run the sensitive signing environment directly. Instead, it launches `pl.kir.szafirhost` as a separate, tightly sandboxed Flatpak with minimal permissions, which keeps the browser integration layer isolated from the host runtime.

## Repository Layout

This repository also acts as the main issue tracker for the Flatpak packages. The manifests and supporting files live in submodules.

## Cloning

```bash
git clone --recurse-submodules https://github.com/deno/szafir-flatpak.git
```

## License

Szafir and SzafirHost are proprietary software by KIR. SzafirHostProxy is licensed under GPL-2.0-only.