# Budowanie instalatora macOS

Komendy wykonuj w katalogu głównym repozytorium. Pakiet `.app` zawiera program, moduły i zależności; `.dmg` służy do przeniesienia go na inny Mac. Build zawiera aktualny stan plików w katalogu roboczym, także zmiany niezacommitowane.

## Wymagania

- Xcode Command Line Tools (`xcode-select --install`), CMake, Homebrew i biblioteki projektu. Lista opcji i zależności pełnego buildu jest w `.github/workflows/build_all.yml` w zadaniach `build_macos_intel` i `build_macos_arm`.
- Na Macu Intel Homebrew ma zwykle prefiks `/usr/local`, na Apple silicon `/opt/homebrew`. `pkg-config --modversion volk glfw3 fftw3f` powinno zwrócić wersje. Gdy brakuje VOLK: `brew install volk`. Na testowanym Macu Intel Homebrew próbował przy okazji przebudować wiele zależności; wariant ze źródeł jest poniżej.
- Dla urządzeń SDR mogą być potrzebne dodatkowe biblioteki i sterowniki, np. SDRplay API. CMake pokaże, których modułów nie udało się włączyć.

## Build natywny i DMG

Na Macu Intel ustaw `ARCH=x86_64`; na Apple silicon `ARCH=arm64`:

```bash
ARCH=$(uname -m)
cmake -S . -B "build-macos-$ARCH" -DCMAKE_OSX_ARCHITECTURES="$ARCH" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DUSE_BUNDLE_DEFAULTS=ON -DOPT_BUILD_AUDIO_SOURCE=OFF -DOPT_BUILD_AUDIO_SINK=OFF -DOPT_BUILD_PLUTOSDR_SOURCE=OFF
cmake --build "build-macos-$ARCH" -j 4
bash make_macos_bundle.sh "build-macos-$ARCH" "SDR++Brown-$ARCH.app"
hdiutil create -volname "SDR++ Brown" -srcfolder "SDR++Brown-$ARCH.app" -ov -format UDZO "sdrpp-brown-$ARCH.dmg"
```

Skrypt pomija moduły, których nie zbudowano. Sprawdź zatem listę dostępnych źródeł i dekoderów w aplikacji oraz wynik konfiguracji CMake. Po każdej zmianie kodu powtórz build i tworzenie `.app` oraz `.dmg`; stare DMG nie aktualizuje się samo.

Na testowanym Macu Apple silicon brakowało `librtlsdr`. Dodaj `-DOPT_BUILD_RTL_SDR_SOURCE=OFF` do konfiguracji albo zainstaluj tę bibliotekę przed buildem, jeśli potrzebujesz źródła RTL-SDR.

Jeśli `pkg-config` nie znajduje `volk`, a `brew install volk` jest zbyt czasochłonne, można zbudować VOLK osobno. W wersji 3.3.0 testowanej na Intelu narzędzie `volk_profile` nie budowało się z obecnym `fmt`, więc poniższy wariant pomija tylko narzędzia pomocnicze, a buduje bibliotekę potrzebną aplikacji:

```bash
git clone --depth 1 --branch v3.3.0 https://github.com/gnuradio/volk.git /tmp/sdrpp-volk
sed -i '' 's/^add_subdirectory(apps)$/# add_subdirectory(apps)/' /tmp/sdrpp-volk/CMakeLists.txt
cmake -S /tmp/sdrpp-volk -B /tmp/sdrpp-volk-build -DCMAKE_INSTALL_PREFIX=/tmp/sdrpp-volk-install -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTING=OFF
cmake --build /tmp/sdrpp-volk-build -j 8
cmake --install /tmp/sdrpp-volk-build
export PKG_CONFIG_PATH="/tmp/sdrpp-volk-install/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
```

Przy konfiguracji SDR++ dodaj wtedy `-DCMAKE_PREFIX_PATH=/tmp/sdrpp-volk-install`. Zachowaj ten katalog do czasu utworzenia `.app`, by skrypt skopiował bibliotekę do pakietu.

Sprawdzenie architektury i podpisu:

```bash
lipo -archs "SDR++Brown-$ARCH.app/Contents/MacOS/sdrpp"
codesign --verify --deep --strict --verbose=2 "SDR++Brown-$ARCH.app"
hdiutil verify "sdrpp-brown-$ARCH.dmg"
```

Podpis skryptu jest doraźny (`codesign -s -`). Do dystrybucji poza własnymi komputerami potrzebne są podpis Developer ID i notarizacja Apple. Otwórz DMG, przeciągnij aplikację do `/Applications`. Przy lokalnym, nienotaryzowanym buildzie macOS może wymagać otwarcia przez menu kontekstowe → Otwórz.

## Mac Apple silicon w sieci

Jeśli używasz hosta skonfigurowanego jako `macbook-air`, zsynchronizuj źródła z Maca Intel, wykluczając pliki buildu, po czym wykonaj powyższe komendy przez SSH na Apple silicon:

```bash
rsync -az --exclude='.git/' --exclude='build*/' --exclude='*.app/' --exclude='*.dmg' --exclude='*.apk' --exclude='android/app/.cxx/' --exclude='android/app/build/' ./ macbook-air:~/Documents/GitHub/SDRPlusPlusBrown/
ssh macbook-air 'cd ~/Documents/GitHub/SDRPlusPlusBrown && PATH=/opt/homebrew/bin:$PATH /opt/homebrew/bin/cmake -S . -B build-macos-arm64 -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DUSE_BUNDLE_DEFAULTS=ON -DOPT_BUILD_AUDIO_SOURCE=OFF -DOPT_BUILD_AUDIO_SINK=OFF -DOPT_BUILD_PLUTOSDR_SOURCE=OFF -DOPT_BUILD_RTL_SDR_SOURCE=OFF && PATH=/opt/homebrew/bin:$PATH /opt/homebrew/bin/cmake --build build-macos-arm64 -j 4 && bash make_macos_bundle.sh build-macos-arm64 SDR++Brown-arm64.app && hdiutil create -volname "SDR++ Brown" -srcfolder SDR++Brown-arm64.app -ov -format UDZO sdrpp-brown-arm64.dmg'
scp macbook-air:~/Documents/GitHub/SDRPlusPlusBrown/sdrpp-brown-arm64.dmg .
```

Pełny build universal wymaga zgodnych wersji **każdego** pliku Mach-O (program, moduły i biblioteki) dla obu architektur. Samo połączenie `sdrpp` przez `lipo` nie wystarczy. Gdy jedna zależność dostępna jest tylko dla jednej architektury, wydaj dwa osobne DMG.

## Android

Instrukcja tworzenia APK i instalowania na telefonie jest w [AGENTS-android.md](./AGENTS-android.md). W skrócie: uruchom Docker Desktop, zbuduj `gradle :app:assembleDebug` w obrazie `android-sdr-kit:latest`, sprawdź `BUILD SUCCESSFUL` i zainstaluj `android/app/build/outputs/apk/debug/app-debug.apk` poleceniem `adb -s <serial> install -r -t ...`. Serial telefonu ustal przez `adb devices -l`. Debug APK nadaje się do ręcznej instalacji; publikacja w sklepie wymaga osobnego klucza wydawniczego i buildu release.
