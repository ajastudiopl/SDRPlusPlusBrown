# Aktualizacja aplikacji Android na telefonach

Jak po każdej zmianie w kodzie zbudować APK SDR++ Brown w Dockerze i wgrać je na telefony.
Instrukcja jest dla człowieka i dla kolejnego agenta. Komendy uruchamiaj w głównym katalogu repozytorium.

## Skrót: aktualizacja, gdy wszystko jest już gotowe

Uruchom Docker Desktop, podłącz telefon przez USB albo adb bezprzewodowo, a potem:

```bash
docker run --rm -v "$PWD":/work -v /tmp:/hosttmp -w /work/android android-sdr-kit:latest bash -o pipefail -lc 'gradle :app:assembleDebug -PsdrKitRoot=/sdr-kit -Pandroid.injected.build.abi=arm64-v8a --no-daemon --console=plain 2>&1 | tee /hosttmp/sdrpp-android-rebuild.log | grep -E "error:|CMake Error|BUILD |What went wrong" | tail -20'
```

Na końcu musi być `BUILD SUCCESSFUL`. Pełny log trafia do `/tmp/sdrpp-android-rebuild.log`.

```bash
adb devices -l
```

```bash
adb -s R5CW1387W5A install -r -t android/app/build/outputs/apk/debug/app-debug.apk
```

Poprawna instalacja kończy się słowem `Success`. Ustawienia aplikacji zostają, bo `-r` nadpisuje istniejącą wersję.

Na ostatnim uruchomieniu kompilacja przyrostowa trwała około 2 minut 20 sekund.

## Telefony

| Telefon | Model | Serial adb | Adres w sieci |
|---|---|---|---|
| Samsung A23 5G | SM-A236B | `R5CW1387W5A` | `192.168.15.146` (router może go zmienić) |
| Samsung A32 5G | SM-A326B | `RFCR80W7XYM` | sprawdź na telefonie |

Żeby wgrać na A32, użyj tej samej komendy z serialem `RFCR80W7XYM`.

Wgrywaj zawsze z `-s <serial>`. Jeśli uruchomiony jest też emulator x86_64, instalacja bez serialu może trafić w niego, a to APK ma tylko bibliotekę ARM64.

## Czego potrzeba

| Narzędzie | Do czego | Skąd |
|---|---|---|
| Docker Desktop | budowanie APK | docker.com, musi być uruchomiony |
| Obraz `android-sdr-kit:latest` | Android SDK, NDK, Gradle 7.3.3, Java 11 i biblioteki SDR w `/sdr-kit` | budowany raz, patrz niżej |
| adb | wgrywanie na telefon | Android SDK (`~/Library/Android/sdk/platform-tools`) |

Telefon musi mieć włączone Opcje programisty i Debugowanie USB. Przy pierwszym połączeniu zaakceptuj monit na ekranie telefonu.

## Budowanie obrazu Dockera (tylko raz)

Sprawdź, czy obraz już istnieje:

```bash
docker images android-sdr-kit
```

Jeśli lista jest pusta, zbuduj obraz poza katalogiem repozytorium. To ta sama definicja, której używa CI w `.github/workflows/build_all.yml`:

```bash
git clone https://github.com/sannysanoff/android-sdr-kit
```

```bash
docker build --platform linux/amd64 --progress=plain -t android-sdr-kit android-sdr-kit
```

Obraz zajmuje około 6,4 GB, a budowanie jest długie, bo kompiluje biblioteki SDR dla czterech architektur. Tych dwóch komend nie uruchamiano przy pisaniu tej instrukcji: obecny obraz na tym komputerze ma już 10 dni. Trzeba zbudować go ponownie tylko wtedy, gdy zmienia się zestaw bibliotek w `/sdr-kit`. Zmiana kodu tego nie wymaga.

## Co robi komenda budowania

- `-v "$PWD":/work` montuje repozytorium w kontenerze, więc gotowe APK pojawia się na Macu w `android/app/build/outputs/apk/debug/app-debug.apk`.
- `--rm` usuwa kontener po zakończeniu, a wyniki kompilacji zostają w repozytorium, dlatego kolejne budowanie jest przyrostowe.
- `-PsdrKitRoot=/sdr-kit` wskazuje biblioteki SDR z obrazu.
- `-Pandroid.injected.build.abi=arm64-v8a` buduje tylko ARM64, co wystarcza na oba telefony i skraca czas. Bez tej opcji Gradle buduje wszystkie cztery architektury.

## Adb bezprzewodowo

Sposób przez USB (działa do restartu telefonu):

```bash
adb -s R5CW1387W5A tcpip 5555
```

```bash
adb connect 192.168.15.146:5555
```

Sposób bez kabla: na telefonie wejdź w Opcje programisty, potem Debugowanie bezprzewodowe, potem Paruj urządzenie za pomocą kodu. Telefon pokaże adres, port i kod do parowania (raz na komputer):

```bash
adb pair <adres>:<port_parowania> <kod>
```

```bash
adb connect <adres>:<port_z_ekranu_debugowania>
```

Port parowania i port połączenia to dwa różne numery i zmieniają się po ponownym włączeniu debugowania.

Przy połączeniu bezprzewodowym serialem w `adb -s` jest `adres:port` z `adb devices -l`.

Jeśli telefonu nie widać, a `adb mdns services` nic nie pokazuje, sprawdź ogłoszenia w sieci:

```bash
dns-sd -B _adb-tls-connect._tcp local.
```

Zatrzymaj tę komendę przez Ctrl+C.

## Sprawdzenie po instalacji

```bash
adb -s R5CW1387W5A shell dumpsys package org.sdrppbrown.sdrppbrown | grep lastUpdateTime
```

```bash
adb -s R5CW1387W5A shell monkey -p org.sdrppbrown.sdrppbrown -c android.intent.category.LAUNCHER 1
```

```bash
adb -s R5CW1387W5A exec-out screencap -p > /tmp/sdrpp.png
```

Logi aplikacji i awarie:

```bash
adb -s R5CW1387W5A logcat -d -t 400 | grep -E "SDR\+\+|FATAL|SIGSEGV"
```

Czas w `lastUpdateTime` powinien odpowiadać właśnie wykonanej instalacji.

## Typowe problemy

| Objaw | Przyczyna | Co zrobić |
|---|---|---|
| `Could not find or load main class org.gradle.wrapper.GradleWrapperMain` | Uruchomiono `./gradlew`. W repozytorium nie ma `gradle/wrapper` | Budować tylko w Dockerze, komendą ze skrótu |
| `Cannot connect to the Docker daemon` | Docker Desktop nie działa | Uruchomić go i poczekać na start |
| `Unable to find image 'android-sdr-kit:latest'` | Brak obrazu | Zbudować obraz |
| `adb: device '...' not found` | Telefon odłączony albo połączenie wygasło | `adb devices -l`, potem podłączyć ponownie |
| Telefon ma status `unauthorized` | Nie zaakceptowano monitu | Zaakceptować monit na telefonie |
| `INSTALL_FAILED_NO_MATCHING_ABIS` | APK z samym ARM64 trafia na emulator x86_64 | Wskazać telefon przez `-s` |
| `INSTALL_FAILED_UPDATE_INCOMPATIBLE` | Zainstalowana wersja ma inny podpis | Odinstalowanie kasuje ustawienia aplikacji, więc najpierw upewnij się, że można je stracić |
| Telefon pokazuje starą wersję | Budowanie się nie powiodło, a wgrano poprzednie APK | Sprawdzić `BUILD SUCCESSFUL` i czas pliku APK |

Wersje debug podpisuje `android/app/debug.keystore` z repozytorium, więc kolejne aktualizacje mają ten sam podpis.

## Zasady dla agentów

- Nie budować przez `gradlew` ani przez Gradle z Maca. Obsługiwane jest budowanie w obrazie `android-sdr-kit`.
- Przed instalacją sprawdzić `adb devices -l` i wskazać telefon serialem. Numer modelu jest widoczny w kolumnie `model:`, więc nie zakładać, że podłączony telefon to ten, o który chodzi.
- APK zawiera wszystkie niescommitowane zmiany z katalogu roboczego, nie tylko bieżące zadanie. Trzeba to zgłosić użytkownikowi, gdy takie zmiany są.
- Nie commitować `android/app/.cxx/`. Nie pushować bez wyraźnej prośby (patrz `AGENTS.md`).
- Zmianę sprawdzać na telefonie: `input tap` przez adb i zrzut ekranu. Współrzędne odnosić do pełnej rozdzielczości zrzutu, a tryb poziomy A23 to 2408x1080.
- Testy dotyku przez `adb shell input tap` przestrajają prawdziwy odbiornik, więc zmieniają częstotliwość zapisaną w aplikacji.
