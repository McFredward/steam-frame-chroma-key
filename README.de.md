# Steam Link Native Passthrough

<p align="center">
  <img src="docs/img/steam-link-icon.png" width="56" height="56" alt="Steam Link">
  &nbsp;&nbsp;
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/img/steam-frame-logo-dark.svg">
    <img src="docs/img/steam-frame-logo.svg" width="240" alt="Steam Frame">
  </picture>
</p>

<p align="center">
  <a href="README.md"><img src="docs/img/flag-en.png" width="24" alt="English">&nbsp;English</a>
  &nbsp;&nbsp;|&nbsp;&nbsp;
  <img src="docs/img/flag-de.png" width="24" alt="Deutsch">&nbsp;<b>Deutsch</b>
</p>

> [!IMPORTANT]
> **Unterstütztes SteamVR auf der Frame: 2.18.2 · Build 1791331621**

**Für Steam Frame.** Hole unterstützte PC-VR-Spiele über Steam Link in dein Zimmer.

## Natives Passthrough — keine Farbersetzung

Anders als bei Virtual Desktops Farbersetzung bleiben alle Spielfarben erhalten:
Das Spiel teilt der Frame mit, welche Bildbereiche durchsichtig sind und wie stark.
Die Brille verbindet diese Information mit ihrem eigenen Live-Kamerabild, sodass
sich auch halbtransparente Objekte mit deinem echten Zimmer mischen können.

**[Auf der Frame installieren](docs/INSTALL.md#directly-on-the-frame)** ·
**[Über SSH installieren](docs/INSTALL.md#from-a-pc-over-ssh)** ·
[Kompatibilität](docs/COMPATIBILITY.md)

Die weiterführenden Anleitungen sind auf Englisch.

## Unterstützte Spiele

| Spiel | Benötigter Mod | Mindestversion |
|---|---|---|
| Gloomhaven Digital | [GloomhavenVR](https://github.com/McFredward/GloomhavenVR) | **1.2.0** (noch nicht veröffentlicht) |

Andere Spiele benötigen eine eigene Integration. Dieser Patch allein ergänzt
kein Mixed Reality in einem unveränderten Spiel.

## Voraussetzungen

| Voraussetzung | Unterstützt |
|---|---|
| Einrichtung | Steam Frame mit Streaming von einem Windows-PC über Steam Link |
| SteamVR auf der Frame | **2.18.2 — Build 1791331621** |

**Experimentell:** Manche Kanten und Bewegungen brauchen noch Verbesserungen.
Siehe [bekannte Grenzen und Update-Unterstützung](docs/COMPATIBILITY.md).

## Loslegen

1. Installiere den Patch [im Desktop-Modus der Frame](docs/INSTALL.md#directly-on-the-frame)
   oder [per SSH vom PC](docs/INSTALL.md#from-a-pc-over-ssh).
2. Starte den PC-VR-Stream und aktiviere natives Passthrough im unterstützten Spiel
   oder Mod. Folge dessen Anleitung für Mixed Reality.

Die Installation bleibt zwischen Spielsitzungen auf der Frame erhalten.
Du kannst Passthrough jederzeit im Spiel ausschalten.

[Aktualisieren](docs/INSTALL.md#update) · [Deinstallieren](docs/INSTALL.md#uninstall) ·
[Hilfe bei Problemen](docs/INSTALL.md#recovery)

## Für Entwickler

[Eigenes Spiel integrieren](docs/DEVELOPING.md) ·
[Laufzeit-Kompatibilität und Grenzen](docs/COMPATIBILITY.md)

## Credits

Basiert auf [Jake Luries steam-frame-chroma-key](https://github.com/spyjacob/steam-frame-chroma-key).
Die Historie und [MIT-Lizenz](LICENSE) bleiben erhalten; die native Transparenz
entwickeln McFredward und Mitwirkende in diesem Projekt weiter.

Inoffizielles Projekt ohne Verbindung zu Valve.
