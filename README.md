English version: [README.en.md](README.en.md)

# szafir-flatpak

<div align="center">
  <p><strong>Nieoficjalny Flatpak dla pakietu podpisu elektronicznego Szafir od KIR.</strong></p>
  <p>Aplikacja desktopowa do podpisu, most do integracji z przeglądarkami i obsługa native hosta przyjazna dla Flatpaka w jednym repozytorium.</p>
  <p>
    <a href="#screenshots">Zrzuty ekranu</a> |
    <a href="#quick-start">Szybki start</a> |
    <a href="#browser-signing-stack">Podpis w przeglądarce</a>
  </p>
</div>

> [!IMPORTANT]
> Nieoficjalna dystrybucja programu KIR Szafir i SzafirHost. Repozytorium nie zawiera żadnych plików, które naruszają prawa autorskie osób trzecich. Flatpak nie jest wspierany przez spółkę KIR, wszelkie problemy z instalacją proszę zgłaszać w tym projekcie.

<a id="screenshots"></a>

## Zrzuty ekranu

<table>
  <tr>
    <td width="50%" valign="top">
      <img width="100%" src="https://raw.githubusercontent.com/deno/flathub/516be0b374b15331e2183221f539895c0076f0db/screenshots/en/home.png" alt="Ekran główny aplikacji Szafir" />
    </td>
    <td width="50%" valign="top">
      <img width="100%" src="https://raw.githubusercontent.com/deno/flathub/516be0b374b15331e2183221f539895c0076f0db/screenshots/en/signing_pades.png" alt="Proces podpisu PAdES w Szafir" />
    </td>
  </tr>
  <tr>
    <td align="center"><strong>Samodzielna aplikacja desktopowa</strong><br />Składaj i weryfikuj kwalifikowane podpisy elektroniczne w kliencie Szafir spakowanym jako Flatpak.</td>
    <td align="center"><strong>Proces podpisywania dokumentu</strong><br />Główny proces podpisu pozostaje w pełni dostępny w sandboxowanej aplikacji desktopowej.</td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <img width="100%" src="https://raw.githubusercontent.com/deno/flathub/67e5b61c60a8e0788c552b73af4c60df8eead385/screenshots/sdk_permission.png" alt="Monit uprawnień przeglądarki w SzafirHostProxy" />
    </td>
    <td width="50%" valign="top">
      <img width="100%" src="https://raw.githubusercontent.com/deno/flathub/67e5b61c60a8e0788c552b73af4c60df8eead385/screenshots/sdk_pin.png" alt="Monit PIN w SzafirHostProxy" />
    </td>
  </tr>
  <tr>
    <td align="center"><strong>Most uprawnień dla przeglądarki</strong><br />SzafirHostProxy podpina wspierane przeglądarki do stosu podpisu, w tym przeglądarki zainstalowane jako Flatpak.</td>
    <td align="center"><strong>Podpisywanie z poziomu WWW</strong><br />Po zainstalowaniu wymaganych komponentów procesy podpisu uruchamiane ze stron WWW nadal mogą dotrzeć do bezpiecznego hosta.</td>
  </tr>
</table>

<a id="quick-start"></a>

## Szybki start

Dodaj repozytorium Flatpak raz, a potem zainstaluj potrzebne pakiety:

```bash
flatpak remote-add --if-not-exists --from szafir https://deno.github.io/szafir-flatpak/szafir.flatpakrepo
```

```bash
flatpak install szafir pl.kir.szafir
flatpak install szafir pl.kir.szafirhost
flatpak install szafir pl.deno.kir.szafirhostproxy
```

> [!TIP]
> Jeśli potrzebujesz tylko samodzielnej aplikacji desktopowej, zainstaluj `pl.kir.szafir`. Do podpisywania w przeglądarce zainstaluj dwa ostatnie pakiety.

Pakiety aktualizujesz poleceniem:

```bash
flatpak update
```

## Przegląd pakietów

| Pakiet | Opis |
| --- | --- |
| `pl.kir.szafir` | Aplikacja do weryfikacji i składania podpisów elektronicznych |
| `pl.kir.szafirhost` | Minimalny wrapper dla SzafirHost z ograniczonymi uprawnieniami. Wywoływany przez helper `pl.deno.kir.szafirhostproxy` |
| `pl.deno.kir.szafirhostproxy` | Automatyczna integracja z przeglądarkami w tym zainstalowanymi jako Flatpak. Wywołuje `pl.kir.szafirhost` i udostępnia przeglądarce. |

## Aplikacja desktopowa

`pl.kir.szafir` to aplikacja do weryfikowania i składania elektronicznych podpisów wydawana przez firmę Krajową Izbę Rozliczeniową (KIR).

> [!NOTE]
> Przy pierwszym użyciu otwórz ustawienia profilu podpisu, wyłącz domyślną konfigurację komponentu technicznego i dodaj bibliotekę Graphite PKCS#11 z `/app/extra/libCCGraphiteP11.2.0.5.6.so`.

> [!TIP]
> Domyślnie aplikacja ma dostęp do folderu Dokumenty. Użyj Flatseal, jeśli chcesz podpisywać pliki z innych lokalizacji.

<a id="browser-signing-stack"></a>

## Podpisywanie w przeglądarce

Jeśli chcesz używać podpisu elektronicznego bezpośrednio w przeglądarce, zainstaluj komponenty hosta oraz oficjalne rozszerzenie KIR.

### Jak to działa

| Komponent | Co robi | Uwagi |
| --- | --- | --- |
| `pl.kir.szafirhost` | Dostarcza właściwe środowisko hosta do podpisu po stronie systemu | Samodzielnie wymaga ręcznej konfiguracji native hosta i słabo współpracuje z przeglądarkami Flatpakowymi |
| `pl.deno.kir.szafirhostproxy` | Obsługuje integrację z przeglądarką i manifesty native messaging | Uruchamia się na żądanie, wspiera przeglądarki systemowe i Flatpakowe oraz udostępnia sterowanie skalowaniem z ikony w zasobniku |

SzafirHostProxy automatycznie instaluje wymagane manifesty. W razie potrzeby można je ręcznie usunąć poleceniem:

```bash
flatpak run pl.deno.kir.szafirhostproxy --uninstall
```

Możliwe jest też ręczne wymuszenie ponownej instalacji w razie potrzeby poleceniem:

```bash
flatpak run pl.deno.kir.szafirhostproxy --install
```

Wymagane tylko w razie problemów z integracją przeglądarki.

### Wymagane rozszerzenia przeglądarki

- Chrome i Chromium: [Szafir SDK Web on Chrome Web Store](https://chromewebstore.google.com/detail/szafir-sdk-web/gjalhnomhafafofonpdihihjnbafkipc)
- Firefox: [Szafir SDK Web for Firefox (.xpi)](https://www.elektronicznypodpis.pl/download/webmodule/firefox/szafir_sdk_web-current.xpi)

### Wspierane przeglądarki

| Przeglądarka | Host | Flatpak |
| --- | --- | --- |
| Mozilla Firefox | ✅ | ✅ |
| LibreWolf | ✅ | ✅ |
| Waterfox | ✅ | ✅ |
| Google Chrome | ✅ | ✅ |
| Google Chrome Dev | ✅ | ✅ |
| Chromium | ✅ | ✅ |
| Ungoogled Chromium | ✅ | ✅ |

## Model bezpieczeństwa

SzafirHostProxy nie uruchamia bezpośrednio wrażliwego środowiska podpisu. Zamiast tego uruchamia `pl.kir.szafirhost` jako osobny, mocno ograniczony Flatpak z minimalnymi uprawnieniami, dzięki czemu warstwa integracji z przeglądarką pozostaje odseparowana od runtime hosta.

## Układ repozytorium

To repozytorium pełni także rolę głównego issue trackera dla paczek Flatpak. Manifesty i pliki pomocnicze znajdują się w submodułach.

<a id="cloning"></a>

## Klonowanie

```bash
git clone --recurse-submodules https://github.com/deno/szafir-flatpak.git
```

## Licencja

Szafir i SzafirHost są własnościowym oprogramowaniem KIR. SzafirHostProxy jest objęty licencją GPL-2.0-only.
