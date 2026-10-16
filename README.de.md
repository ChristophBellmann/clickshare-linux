# ClickShare unter Linux

Teile deinen **Linux-Desktop inklusive Ton** mit einem **ClickShare Button**
(USB-Dongle) auf ein Barco-ClickShare-Raumsystem. Barco bietet die
ClickShare-App nur für Windows und macOS an. Dieses Projekt bringt die
Windows-App unter Wine zum Laufen.

> 🇬🇧 English version: [README.md](README.md). Die technischen Details stehen
> (auf Englisch) in [docs/TECHNICAL.md](docs/TECHNICAL.md).

| | Status |
|---|---|
| Button wird erkannt, Kopplung mit dem Raum | ✅ |
| Desktop-Bild auf dem Raumbildschirm | ✅ (über den mitgelieferten Shim) |
| Ton im Raum | ✅ |
| Einzelnes Fenster teilen | ❓ ungetestet (Vorschau bleibt schwarz) |
| Getestet mit | Pop!_OS 22.04, GNOME unter **X11**, Wine 11.0 (WineHQ), ClickShare-App 4.36.0, Button USB-ID `0600:00ce` |

> **Hinweis:** Dies ist ein inoffizielles Community-Projekt. Es steht in keiner
> Verbindung zu Barco und wird von Barco weder unterstützt noch empfohlen.
> „ClickShare“ und „Barco“ sind Marken der Barco N.V. Das Repository enthält
> **keine Barco-Software**; du brauchst einen eigenen ClickShare-Installer.

---

## Funktionsweise

```
 Linux-Desktop (X11)
        │  GDI BitBlt (Wine)
        ▼
 ┌───────────────────┐   DuplicateOutput()   ┌────────────────────────────┐
 │ csdup.dll (Shim)  │◀─────────────────────│ clickshare_native.exe      │
 │ IDXGIOutput-      │──── D3D11-Textur ───▶│ (ClickShare-App, Wine 11)  │
 │ Duplication       │                       └──────────────┬─────────────┘
 └───────────────────┘                     HID (hidraw)     │ Videostrom
                                                            ▼
 PipeWire/PulseAudio ── USB-Audio ──────────────▶ ClickShare Button ── WLAN ──▶ Basis ──▶ Raumbildschirm
```

1. **Die ClickShare-App** (`clickshare_native.exe`) ist ein natives
   Qt/C++-Programm. Sie wird direkt aus Barcos `ClickShare_Setup.exe`
   ausgepackt. Das umgeht den Squirrel-Installer, der das volle .NET Framework
   braucht und unter Wine nicht funktioniert.
2. **Wine 11** gibt der App über `hidraw` Zugriff auf die HID-Schnittstellen des
   Buttons. Eine udev-Regel vergibt die nötigen Rechte.
3. **Bildschirmaufnahme:** Die App nimmt den Desktop ausschließlich über
   *DXGI Desktop Duplication* auf (`IDXGIOutput1::DuplicateOutput`). Wine hat
   diese Funktion nicht implementiert, deshalb bleibt der Raumbildschirm schwarz.
   Die kleine Shim-DLL `csdup.dll` rüstet sie nach: Sie liest den Bildschirm per
   GDI `BitBlt` aus (das funktioniert unter Wine), zeichnet den Mauszeiger ein
   und lädt jedes Bild in eine D3D11-Textur für die App. `inject.exe` startet
   die App und lädt den Shim hinein.
4. **Ton:** Der Button ist zugleich eine USB-Soundkarte. Während einer Freigabe
   gibt er alles, was an ihn ausgegeben wird, im Raum wieder. Der Launcher macht
   ihn zur Standard-Audioausgabe.

---

## Voraussetzungen

- Ein **ClickShare Button**, der sich mit einer ClickShare-Basis verbindet.
  Getestet mit einem Button mit der USB-ID `0600:00ce` und Raumkamera
  `0600:0114`.
- `ClickShare_Setup.exe`, der Windows-Installer der ClickShare-App (siehe
  [Schritt 3](#3-clickshare_setupexe-besorgen)).
- Linux mit **X11-Sitzung**. Wayland ist ungetestet.
- **Wine ≥ 9** (getestet: 11.0). Das Wine 6.0 aus Ubuntu 22.04 ist **zu alt**:
  Die App stürzt beim Start ab.
- `python3`, `curl`, `pactl` (PipeWire oder PulseAudio), optional `7z` (`p7zip-full`).
- Einmalig Root-Rechte für die udev-Regel und gegebenenfalls für Wine.

---

## Installation

### 1. Wine

Version prüfen:

```sh
wine --version
```

Ist sie älter als 9, installierst du das aktuelle WineHQ-Wine. Auf Ubuntu und
Ablegern wie Pop!_OS oder Mint:

```sh
sudo sh scripts/install-winehq.sh
```

### 2. USB-Rechte (udev)

```sh
sudo sh scripts/install-udev-rule.sh
```

Die Regel [`udev/70-clickshare.rules`](udev/70-clickshare.rules) gibt dem am
Rechner angemeldeten Benutzer Zugriff auf alle Barco-Geräte (Hersteller-ID
`0600`). **Den Button danach abziehen und wieder einstecken.**

### 3. `ClickShare_Setup.exe` besorgen

Du brauchst den Windows-Installer der ClickShare-App (getestet: 4.36.0):

- *ClickShare App für Windows* von Barcos Website herunterladen (barco.com →
  ClickShare → Apps/Downloads).
- Oder den Button einstecken und den Launcher darauf unter Wine starten:
  `wine /media/$USER/ClickShare/ClickShare_for_Windows.exe`. EULA annehmen und
  auf *Installieren* klicken. Der Launcher lädt `ClickShare_Setup.exe` nach
  `~/Downloads`. Die anschließende .NET-Installation **abbrechen** bzw. mit
  `wineserver -k` beenden: Sie hängt unter Wine und wird nicht gebraucht.

### 4. Einrichten

```sh
sh scripts/setup.sh ~/Downloads/ClickShare_Setup.exe
```

Das Skript erledigt drei Dinge:

1. Es legt einen eigenen Wine-Prefix in `~/.wine-clickshare` an. Den Ort kannst
   du mit `CLICKSHARE_PREFIX=...` ändern.
2. Es packt die App in den Prefix aus.
3. Es baut `build/csdup.dll` und `build/inject.exe`. Ohne installiertes
   MinGW-w64 lädt es dafür eine feste Zig-Version nach `.toolchain/`, ohne
   Root-Rechte.

---

## Benutzung

1. ClickShare Button einstecken.
2. Starten:
   ```sh
   sh clickshare.sh
   ```
   Der Launcher macht drei Dinge:
   - Er stellt den Button als Standard-Audioausgabe ein. Das kannst du mit
     `CLICKSHARE_NO_AUDIO=1` abschalten.
   - Er startet die App.
   - Er lädt nach 3 Sekunden den Shim.
3. Die App zeigt *Für den Raum freigeben*. Den Button drücken oder auf
   **Desktop 1** klicken.

Danach die Audioausgabe wieder auf die eigenen Lautsprecher stellen, zum
Beispiel unter *Einstellungen → Ton*.

---

## Fehlerbehebung

| Symptom | Ursache / Lösung |
|---|---|
| App meldet *„Schließen Sie einen ClickShare Button an“* | Kein HID-Zugriff. udev-Regel prüfen (Schritt 2), Button neu einstecken, `wineserver -k`. |
| Raum zeigt den ClickShare-Rahmen, aber **schwarzes Bild** | Der Shim ist nicht geladen. Prüfe `$WINEPREFIX/drive_c/csdup.log`: Dort muss `patched DuplicateOutput` und `duplication created` stehen. App nur über `clickshare.sh` starten. |
| **Kein Ton** im Raum | Die Standardausgabe ist nicht der Button. Prüfe mit `pactl get-default-sink`. Der Sink-Name ändert sich nach dem Neueinstecken. Ton kommt nur während einer Freigabe an. |
| Absturz beim Start (`RoGetActivationFactory`, `c0000005`) | Wine zu alt. Wine ≥ 9 installieren. |
| Dialog *„.NET Framework Initialization Error“* | Harmlos, mit OK schließen. |
| .NET-Installer hängt bei *„netfx_Full_LDR.mzz“* | `ClickShare_Setup.exe` wurde direkt ausgeführt. Mit `wineserver -k` beenden und stattdessen `scripts/setup.sh` nutzen. |

**Logs:**

- App: `$WINEPREFIX/drive_c/users/$USER/AppData/Local/Barco/ClickShare App/clickshare.log`
- Shim: `$WINEPREFIX/drive_c/csdup.log`

---

## Einschränkungen

- **Nur unter X11 getestet.** Unter Wayland sieht Wines GDI-Aufnahme nur
  X11- bzw. XWayland-Fenster.
- **Einzelne Fenster teilen** ist ungetestet; die Vorschau in der App bleibt
  schwarz. Nutze die Desktop-Freigabe.
- **Etwa 30 fps über CPU-Kopien.** Das reicht gut für Folien und Dokumente,
  ist aber nicht ideal für Videos.
- Funktionen, die .NET brauchen (Kalender/Outlook), und Auto-Update fehlen.

---

## Deinstallation

```sh
rm -rf ~/.wine-clickshare
sudo rm /etc/udev/rules.d/70-clickshare.rules && sudo udevadm control --reload
rm -rf build .toolchain
```

## Lizenz

Der Code in diesem Repository steht unter der [MIT-Lizenz](LICENSE). Die
ClickShare-Software ist proprietäre Software der Barco N.V. und fällt **nicht**
unter diese Lizenz. Für ihre Nutzung gilt Barcos EULA.
