# SDR Remote na iPadOS

Pierwsza wersja klienta do sterowania SDR++ Brown na Macu przez istniejące API HTTP. Działa na iPadOS 17 lub nowszym. Nie przesyła jeszcze dźwięku ani widma na iPada.

## Co działa

- Połączenie z adresem IP i portem Maca, odczyt stanu serwera.
- Odświeżanie stanu, źródła i demodulatora co 5 sekund oraz ręcznie przyciskiem lub gestem przeciągnięcia.
- Start i zatrzymanie odbioru.
- Wybór źródła sygnału oraz demodulatora modułu Radio.
- Wysłanie częstotliwości w MHz oraz przestrajanie krokami.

Bieżąca częstotliwość Maca nie jest dostępna w obecnym API. Pole częstotliwości na iPadzie jest na początku puste; po wysłaniu pole pokazuje ostatnią wartość ustawioną z iPada. Zmiana częstotliwości na Macu nie aktualizuje tego pola. Zmiana źródła może wymagać skonfigurowania sprzętu w aplikacji na Macu.

## Mac

Uruchom SDR++ Brown na Macu. Serwer HTTP domyślnie nasłuchuje na porcie `8080`; można podać inny port przez `--http <port>`. Sprawdź na Macu:

```bash
curl http://localhost:8080/status
```

Sprawdź adres IP Maca w ustawieniach Wi-Fi i wpisz go w aplikacji na iPadzie. Oba urządzenia muszą być w tej samej sieci. Serwer HTTP nie ma uwierzytelniania ani szyfrowania i udostępnia także funkcje diagnostyczne. Używaj go tylko w zaufanej sieci lokalnej; nie przekierowuj portu 8080 do Internetu.

## Build i instalacja

Projekt Xcode jest już wygenerowany w `SDRRemote.xcodeproj`. Do regeneracji po zmianie `project.yml` potrzebny jest `xcodegen`:

```bash
xcodegen generate --spec ios/SDRRemote/project.yml --project ios/SDRRemote
open ios/SDRRemote/SDRRemote.xcodeproj
```

W Xcode wybierz projekt **SDRRemote** → **Signing & Capabilities** → **Automatically manage signing** i wybierz swój zespół Apple. Jeśli Xcode zgłasza brak konta, dodaj Apple ID w **Xcode → Settings → Accounts**. Podłącz iPada, zaakceptuj zaufanie i włącz Developer Mode, gdy iPad o to poprosi. Wybierz iPada jako urządzenie docelowe i naciśnij **Run**. Xcode podpisze i zainstaluje aplikację.

Build do symulatora bez podpisu:

```bash
xcodebuild -project ios/SDRRemote/SDRRemote.xcodeproj -scheme SDRRemote -configuration Debug -sdk iphonesimulator -destination 'platform=iOS Simulator,name=iPad mini (A17 Pro)' CODE_SIGNING_ALLOWED=NO build
```

W tym środowisku build symulatora i build iOS bez podpisu przeszły. Build podpisany dla fizycznego iPada wymaga dodania aktywnego konta zespołu Apple do Xcode; sam certyfikat w pęku kluczy nie wystarczył do utworzenia profilu dla `org.sdrppbrown.remote`.
