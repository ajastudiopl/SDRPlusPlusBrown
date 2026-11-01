# Jak zaktualizować aplikację (Mac, serwer, iPad)

Każdy skrypt sam sprawdza, co się zmieniło w kodzie, i kompiluje tylko to. Jeśli kod jest taki sam, nic się nie kompiluje.
Uruchamiaj z katalogu repozytorium.

| Co | Komenda |
| --- | --- |
| Aplikacja na Macu (z okienkiem) | `./run-macos.sh` |
| Aplikacja na Macu **i** serwer w tle (port 5259) | `./run-macos.sh --server` |
| Aplikacja na iPadzie | `./run-ipad.sh` |

Uwagi:
- `./run-macos.sh --server` restartuje serwer, więc iPad zostanie rozłączony. Połącz się ponownie (`192.168.15.160:5259`).
- `./run-ipad.sh` wymaga iPada podłączonego kablem i **odblokowanego**. Jeśli jest zablokowany, aplikacja i tak się zainstaluje, ale trzeba ją otworzyć ręcznie.
- Serwer obsługuje jednego klienta naraz. Jeśli iPad nie może się połączyć, zamknij inne połączenia (aplikacja na Macu, symulator).
- Zmiana kodu serwera (`core/src/server.cpp`) wymaga `./run-macos.sh --server`. Zmiana samego klienta (interfejs iPada) wymaga tylko `./run-ipad.sh`. Zmiana protokołu wymaga obu.

Uruchomienie bez kompilacji (to, co już jest zainstalowane):

```bash
open -n "/Applications/SDR++ by AJA.app"
```

Serwer w tle (usługa `pl.aja.sdrpp-server`):

```bash
launchctl bootout   gui/$(id -u) ~/Library/LaunchAgents/pl.aja.sdrpp-server.plist   # zatrzymaj
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/pl.aja.sdrpp-server.plist   # uruchom
tail -f ~/Library/Logs/sdrpp-server.log                                              # log serwera
```
