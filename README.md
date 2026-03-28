# szafir-flatpak

Community Flatpak packaging of the Szafir electronic signature suite by KIR (Krajowa Izba Rozliczeniowa).

This repository serves primarily as an **issue tracker** for the Flatpak builds. The actual build manifests live in the submodules below.

## Szafir (Standalone App)

`pl.kir.szafir` is the independent Polish qualified electronic signature desktop application.

**Configuration:**
* Open the signing profile settings, disable the default technical component configuration, and add the Graphite PKCS#11 library from `/app/extra/libCCGraphiteP11.2.0.5.6.so`.
* The app only has access to the Documents folder by default. Run Flatseal to grant additional filesystem access if you need to sign files elsewhere.

## Browser Integration (SzafirHost & Proxy)

To use electronic signatures directly from web browsers, you need the host components and the browser extensions.

### Core Components

* **`pl.kir.szafirhost`**: A thin wrapper around the official SzafirHost application. If you try to use it directly, you must manually configure browser native host manifests, and it **will not work** with Flatpaked browsers.
* **`pl.deno.kir.szafirhostproxy`**: A DBus service built to manage the integration.
  * Automatically configures native host manifests for browsers.
  * Has full support for both host-installed and Flatpaked browsers.
  * Autostarts automatically when installed and a browser tries to use it (no need to run manually).
  * Runs a tray icon where you can **right-click to manage HiDPI scaling options**.
  * To uninstall integration, run: `flatpak run pl.deno.kir.szafirhostproxy --uninstall`

### Browser Extensions

You must also install the official KIR extension in your browser:
* **Chrome/Chromium:** [Szafir SDK Web on Chrome Web Store](https://chromewebstore.google.com/detail/szafir-sdk-web/gjalhnomhafafofonpdihihjnbafkipc)
* **Firefox:** [Szafir SDK Web for Firefox (.xpi)](https://www.elektronicznypodpis.pl/download/webmodule/firefox/szafir_sdk_web-current.xpi)

## Security and Permissions

SzafirHostProxy employs a secure architecture. Rather than running the sensitive signing environment directly, the proxy securely launches `pl.kir.szafirhost` as a separate, strictly sandboxed Flatpak environment with minimal permissions.

## Screenshots

### Szafir

![Szafir application home screen](https://raw.githubusercontent.com/deno/flathub/516be0b374b15331e2183221f539895c0076f0db/screenshots/en/home.png)

### SzafirHostProxy

![SzafirHostProxy signing workflow](https://raw.githubusercontent.com/deno/flathub/67e5b61c60a8e0788c552b73af4c60df8eead385/screenshots/sdk_permission.png)

## Cloning

```bash
git clone --recurse-submodules https://github.com/deno/szafir-flatpak.git
```

## License

Szafir and SzafirHost are proprietary software by KIR. SzafirHostProxy is licensed under GPL-2.0-only.
