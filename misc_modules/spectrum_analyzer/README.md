# Spectrum Analyzer (Looking Glass) — SDR++ by AJA

Szerokopasmowy analizator widma z przemiataniem częstotliwości, działający
podobnie do „Looking Glass” z PortaPacka. Jest modułem SDR++ by AJA
(`misc_modules/spectrum_analyzer`) i ma własną zakładkę obok odbiornika —
nie jest osobną aplikacją i nie zastępuje zwykłego widma/wodospadu.

## Jak uruchomić

1. Moduł buduje się domyślnie (`-DOPT_BUILD_SPECTRUM_ANALYZER=ON`), także w
   buildzie Androida (`spectrum_analyzer.so` jest na liście modułów).
2. Instancja „Spectrum Analyzer” jest w domyślnej konfiguracji; do istniejących
   konfiguracji dodawana jest jednorazowo (flaga `spectrumAnalyzerAdded`
   w `config.json`). Można ją wyłączyć/usunąć w Module Manager.
3. Nad wodospadem są przyciski **Receiver | Spectrum Analyzer** (jedno
   kliknięcie przełącza widok). Zakładka analizatora pokazuje, tak jak zwykły
   widok SDR++, widmo u góry i duży wodospad pod nim, plus jedną linię statusu
   (Start/Stop, stan, zakres, zoom, Tune/Mark).
4. Wszystkie ustawienia są w lewym menu, w pozycji „Spectrum Analyzer”, tak jak
   w innych modułach: urządzenie, tryb, Start/Stop/Pause, presety, a niżej
   rozwijane sekcje (zakres, sweep, gain, widmo, wodospad, markery, sygnały,
   presety, eksport, urządzenie). Przycisk *Show Spectrum Analyzer* przełącza
   widok (działa również w układzie transceivera).

## Urządzenia i tryby

| Urządzenie | Tryb | Jak działa | Status weryfikacji |
|---|---|---|---|
| HackRF One i zgodne (libhackrf) | **Native** | firmware HackRF przemiata sam (`hackrf_init_sweep` + `hackrf_start_rx_sweep`), parametry jak w `hackrf_sweep` | testy na symulowanym urządzeniu; **brak testu na fizycznym HackRF** |
| HackRF ze starym firmware (USB API < 1.02) | Generic | Auto przełącza na Generic z komunikatem | logika przetestowana na symulacji |
| Każde źródło SDR++ (RTL-SDR, Airspy, AirspyHF+, SDRplay, LimeSDR, USRP, SoapySDR, PlutoSDR, HydraSDR, RTL-TCP, SpyServer…) | **Generic (stepped)** | przestrajanie źródła odbiornika segment po segmencie | ścieżka danych sprawdzona na File Source (Linux); **żadne fizyczne SDR nie było testowane** |
| Demo | syntetyczne | sztuczna scena RF, zawsze oznaczona „DEMO – SYNTHETIC SIGNALS, NOT A MEASUREMENT”, także w eksporcie | — |

Tryb **Auto**: HackRF → Native (jeżeli firmware nie wspiera sweep → Generic),
pozostałe urządzenia → Generic. Opcja *Native* jest ignorowana (z komunikatem)
dla urządzeń bez natywnego sweep.

### Dlaczego libhackrf, a nie proces `hackrf_sweep`

* Na Androidzie HackRF jest dostępny wyłącznie przez deskryptor USB nadany
  aplikacji (`hackrf_open_by_fd`); zewnętrzny proces `hackrf_sweep` nie ma do
  niego dostępu i nie istnieje w APK.
* Jedno miejsce posiada uchwyt urządzenia: moduł `hackrf_source` implementuje
  interfejs `sweep::NativeSweepProvider` (`core/src/signal_path/native_sweep.h`,
  udostępniany przez `getInterface()`), więc odbiornik i analizator nie mogą
  otworzyć tego samego HackRF jednocześnie.
* Bloki danych trafiają bezpośrednio do aplikacji (bez parsowania tekstu,
  bez opóźnień potoku, z wykrywaniem odłączenia przez `hackrf_is_streaming`).
* Stare libhackrf (przed 2021) bez `hackrf_start_rx_sweep` są wykrywane przez
  CMake i obsługiwane przez `hackrf_start_rx`.

## Architektura

```
            UI thread                                worker thread              device thread
 ┌──────────────────────────┐     start/stop      ┌─────────────────┐
 │ SpectrumAnalyzerModule   │ ──────────────────▶ │ SweepController │  state machine
 │  (main.cpp)              │ ◀── state, stats ── │  Idle/Starting/ │  Idle→Starting→Sweeping⇄Paused
 │  SpectrumView (ImGui+GL) │                     │  Sweeping/...   │  →Stopping→Idle | Error | Disconnected
 └───────────▲──────────────┘                     └───────┬─────────┘
             │ snapshot()                                 │ open / run / close
 ┌───────────┴──────────────────────────────────┐ ┌───────▼───────────────────┐   ┌──────────────────┐
 │ SweepEngine                                  │ │ SweepBackend              │   │ hackrf_source    │
 │  SegmentProcessor (okno + FFT rdzenia SDR++) │◀│  NativeSweepBackend ──────┼──▶│ NativeSweepProv. │ USB callback
 │  SpectrumAssembler (składanie szerokopasm.)  │ │   (kolejka SPSC bloków)   │   └──────────────────┘
 │  DisplayProcessor (avg, max hold, NF)        │ │  GenericSweepBackend ─────┼──▶ CoreReceiverTuner (SourceManager
 │  WaterfallHistory (pierścień 8-bit)          │ │   (SampleRing, settling)  │    ::tune + IQFrontEnd::bindIQStream)
 │  PeakDetector                                │ │  (Demo: SyntheticTuner)   │
 └──────────────────────────────────────────────┘ └───────────────────────────┘
```

* `src/engine/*` nie zależy od rdzenia SDR++ (testowalny samodzielnie).
  FFT jest wstrzykiwane: w module to plan rdzenia (`dsp::arrays::allocateFFTWPlan`
  — FFTW lub vDSP), w testach FFTW albo referencyjna FFT.
* Pozyskiwanie danych, FFT/składanie i rysowanie są w osobnych wątkach.
  Wątek USB HackRF tylko kopiuje bloki do prealokowanej kolejki (bez blokowania
  i alokacji; przepełnienie = zliczony utracony blok). Generic czyta IQ
  w osobnym wątku do pierścienia, więc łańcuch DSP odbiornika nigdy nie czeka.
* Wodospad to tekstura GPU używana jako bufor pierścieniowy: każdy zakończony
  sweep wysyła jedną linię (`glTexSubImage2D`); pełna przebudowa tylko przy
  zmianie widoku, rozmiaru lub kolorów.

### Składanie widma

* Każdy bin FFT jest umieszczany według swojej rzeczywistej częstotliwości
  (LO + offset binu), więc nakładające się segmenty i różne siatki LO trafiają
  dokładnie w te same punkty siatki wyświetlania.
* Z każdego pomiaru używane są tylko wiarygodne fragmenty: brzegi (zbocze filtra)
  są odrzucane (`usable bandwidth`), a okolica LO (DC spike / przeciek LO) ma
  niższy priorytet niż „czyste” dane z sąsiedniego segmentu. Dla HackRF Native
  używane są dokładnie przedziały `hackrf_sweep`: 2,5–7,5 MHz po obu stronach LO.
* Gdzie segmenty się nakładają: detektor **Peak** (max — wąskie sygnały nigdy
  nie znikają przy zmniejszaniu rozdzielczości) lub **Average** (średnia mocy
  liniowej). Bez wygładzania.
* Punkty, których nie zmierzono, pozostają puste (NaN) — na wykresie przerwa,
  w CSV puste pole.
* Pełny cykl = wszystkie segmenty planu. Ramka z brakującymi segmentami jest
  oznaczona „LAST SWEEP INCOMPLETE” (a linia wodospadu czerwonym znacznikiem
  na lewej krawędzi). Przerwany sweep (Stop/Pause/restart) nie jest publikowany.
  Sweep w toku można pokazać szarą linią z podpisem „sweep in progress (partial)”.
* Poziomy w dBFS skalibrowane tonem (pełnoskalowy ton = 0 dB dla każdego okna).
  Różnice wzmocnienia między segmentami (nierówność toru) **nie są** wyrównywane.

## Parametry

**Frequency Range** — Start/Stop/Center/Span (powiązane), jednostki kHz/MHz/GHz,
walidacja (Stop > Start, minimalny span 1 kHz, zakres urządzenia dla HackRF:
0–7250 MHz strojenia, 1–6000 MHz wg producenta — poza nim ostrzeżenie).
Presety: FM Broadcast 88–108, Airband 118–137, 2 m 144–146, 70 cm 430–440,
UHF 400–470, 1–1000 MHz — są tylko przykładami.

**Sweep Settings**
* *Auto parameters* — rozmiar FFT z rozdzielczości i częstotliwości próbkowania,
  pasmo użyteczne z danych źródła, nakładanie tak, aby obszary DC były pokryte.
* *Resolution (points)* — liczba punktów szerokopasmowego wykresu (1024–65536).
* *FFT Size, Window* (Rectangular/Hann/Blackman/Nuttall/Blackman-Harris),
  *Detector* (Peak/Average), *FFT averaging* (ramki Welcha na segment).
* Generic: *Usable bandwidth %*, *Overlap %*, *Frequency step*, *Dwell time*,
  *Settling time* (zapamiętywany osobno dla każdego źródła; wartości startowe
  zależą od typu źródła i **nie są pomiarem**), *Direction* (Up/Down).
* Native (HackRF): stałe 20 MS/s i krok 20 MHz (tryb interleaved),
  *Dwell (blocks/step)* 1–16, FFT ≤ 4096 (blok 8184 próbek), kierunek tylko w górę,
  settling realizuje firmware.
* *Max sweeps/s* — ograniczenie odświeżania (oszczędza CPU/baterię).

**Spectrum Display** — Min/Max dB, Auto scale, Current/Average/Max hold,
Peak decay (dB/s), Persistence, Noise floor (percentyl widma), Grid, Peaks,
Markers, Sweep in progress.

**Waterfall** — paleta (mapy kolorów SDR++), zakres dynamiki (min/max dB),
Auto levels (domyślnie: zakres podąża za poziomem szumu, regulowany zakres
dynamiki), Brightness, Contrast, Speed (ile pełnych sweepów na linię), History (linie),
Pause display / Freeze, Clear history. Każda linia to co najmniej jeden
kompletny cykl. Pamięć: poziomy 8-bit (~0,8 dB), ograniczenie 48 MB
(np. 4096 punktów × 2048 linii = 8 MB).

**Gain** — HackRF: LNA 0–40 dB (krok 8), VGA 0–62 dB (krok 2), AMP, Bias-T
(zmiany na żywo). HackRF nie ma AGC. Generic: wyświetlane są ustawienia
wybranego źródła odbiornika (gain/AGC/bias-T zgodnie z jego modułem).

## Zarządzanie urządzeniem

Stany: *Idle, Starting, Sweeping, Paused, Stopping, Error, Device Disconnected*.

* HackRF używany przez odbiornik → okno „Device in use”: *Stop receiver and
  start sweep* (opcjonalnie wznowienie odbiornika po zatrzymaniu sweepu).
* Podczas natywnego sweepu przycisk Play odbiornika jest zablokowany, gdy
  wybranym źródłem jest HackRF; dodatkowo `hackrf_source` odmawia startu,
  gdy urządzenie należy do analizatora (i odwrotnie).
* Generic przejmuje źródło odbiornika (z potwierdzeniem, jeśli odbiornik gra),
  wycisza audio i po zakończeniu przywraca źródło, stan Play i częstotliwość.
* Dwa fizyczne SDR: analizator i odbiornik mogą pracować równolegle na różnych
  urządzeniach (np. HackRF #1 sweep, HackRF #2 lub RTL-SDR w odbiorniku).
* Odłączenie USB: brak danych / `hackrf_is_streaming` ≠ TRUE → stan
  *Device Disconnected*, urządzenie jest zwalniane; po ponownym podłączeniu
  *Refresh* i *Start*.

## Interakcja

* Najechanie: częstotliwość, poziom (current/avg/max), poziom kursora, wiek pomiaru.
* Kółko myszy: zoom wokół kursora; przeciąganie: przesuwanie; przyciski − / + / Fit.
  Zoom i przesuwanie **nie zmieniają** zakresu skanowania.
* Dwuklik: przestrojenie odbiornika na wskazaną częstotliwość (sweep jest
  zatrzymywany, urządzenie oddawane odbiornikowi, przełączenie na zakładkę Receiver).
* Prawy klik: menu (Tune, Add/Remove marker, ustaw Start/Stop sweepu, zoom, Fit).
* Ekrany dotykowe (Android): stuknięcie zaznacza częstotliwość (przerywana linia),
  a przyciski **Tune** / **Mark** na pasku narzędzi działają na zaznaczeniu;
  przeciąganie palcem przesuwa widok, dwukrotne stuknięcie stroi odbiornik.
* Markery: nazwa, częstotliwość, aktualny poziom; dodawanie z menu, z listy
  sygnałów lub ręcznie; zapisywane w konfiguracji.
* Podział wykres/wodospad można przeciągać (domyślnie 30% widmo / 70% wodospad).
  Każdy sweep ma w wodospadzie co najmniej 2 piksele (× skala UI) wysokości.

## Wykrywanie sygnałów, presety, eksport

* Peak detection: minimalny poziom, próg nad noise floor, minimalny odstęp,
  minimalna szerokość; lista z akcjami *Tune* i *+Marker*.
* Presety użytkownika zapisują zakres, parametry sweepu, wyświetlania i gain.
* **Snapshot** (folder `<root>/spectrum_snapshots` lub własny):
  * `*.csv` — wartości zmierzone dla każdego punktu siatki sweepu
    (`frequency_hz,current_db,average_db,max_hold_db`) + metadane w nagłówku,
  * `*_spectrum.png`, `*_waterfall.png` (PNG bez kompresji, bez zależności),
  * `*.json` — metadane pomiaru, statystyki, markery, sygnały,
  * `*_signals.json` — model danych dla przyszłej integracji z SDR Patrol
    (`schema: sdrpp-aja.spectrum-analyzer.signals/1`): częstotliwość, poziom,
    odstęp od szumu, szerokość, timestamp, zakres pomiaru, urządzenie,
    backend, parametry sweepu (RBW, FFT, detektor, fs), nazwa markera.
    Integracja sieciowa nie jest zaimplementowana.

## Statystyki w interfejsie

`measured: X sweeps/s, cycle N ms` są **zmierzone** (czas między kolejnymi
zakończonymi cyklami). Liczba segmentów, RBW i krok wynikają z planu
(konfiguracja). *incomplete* — cykle z brakującymi segmentami, *lost* — bloki
utracone przed przetworzeniem (przepełnienie kolejki / USB). Pasek pod
statusem to postęp bieżącego cyklu.

## Ograniczenia

* **Brak testu na fizycznym HackRF ani innym SDR** — patrz „Testy”.
  Zachowanie firmware (kolejność strojeń, czasy) przyjęto wg `hackrf_sweep`
  i libhackrf; prędkość sweepu nie jest obiecywana, tylko mierzona.
* Pojedynczy HackRF nie może jednocześnie przemiatać i odbierać ciągle —
  analizator zatrzymuje odbiornik (za zgodą) i oddaje urządzenie po Stop.
* Generic jest wolny w porównaniu z Native (każdy segment = przestrojenie +
  settling); czas przestrajania zależy od urządzenia i sterownika — ustaw
  *Settling time* tak, aby nie było „duchów” sygnałów z poprzedniego segmentu.
* Źródła sieciowe (RTL-TCP, SpyServer, SDR++ Server, KiwiSDR) działają, ale
  z dużymi opóźnieniami przestrajania; File Source nie zmienia treści przy
  przestrajaniu (przydatny tylko do testów).
* Brak korekcji nierówności charakterystyki toru między segmentami i kalibracji
  do dBm; poziomy są w dBFS.
* Tryb Native wymaga firmware HackRF z USB API ≥ 1.02.
* Eksport PNG jest nieskompresowany; obraz widma nie zawiera opisów osi
  (opis jest w JSON/CSV).

## Testy

Testy silnika (bez rdzenia SDR++, GPU i radia):

```bash
cmake -S misc_modules/spectrum_analyzer/tests -B build-sa-tests
cmake --build build-sa-tests
./build-sa-tests/spectrum_analyzer_tests
```

lub w głównym buildzie z `-DBUILD_TESTS=ON` (cel `spectrum_analyzer_tests`,
CTest). 45 testów: plan segmentów i pokrycie, kalibracja FFT, składanie,
overlap/DC, niepełne i przerwane cykle, przepływ bloków HackRF (symulowany
provider: brak urządzenia, konflikt z odbiornikiem, stare firmware,
odłączenie, utrata bloków), bufor pierścieniowy wodospadu i limit pamięci,
ślady wyświetlania, wykrywanie pików, ustawienia/presety, eksport CSV/JSON/PNG,
kolejki, kontroler (start/stop, wielokrotny start/stop, pauza, odłączenie
i ponowne podłączenie), settling po przestrojeniu, zoom/kursor/markery/jednostki.
Wszystkie testy używają danych syntetycznych — **nie są dowodem poprawnego
działania na HackRF**.

Automatyzacja w działającej aplikacji (serwer HTTP debug):
`GET /module/Spectrum%20Analyzer/command?cmd=<status|devices|select_device|set_range|start|stop|pause|resume|show|hide|tune|export|clear|refresh|set>&args=...`
(akcje są kolejkowane do wątku UI).

## Pomysły na kolejną wersję

* Test i strojenie na fizycznym HackRF (prędkość, czasy settlingu, gain).
* Natywny sweep dla innych urządzeń (np. przez dedykowane API producentów).
* Kalibracja poziomów (dBm) i korekcja charakterystyki między segmentami.
* Wiele zakresów w jednym sweepie (HackRF obsługuje do 10).
* Kompresja PNG, opis osi w eksporcie, nagrywanie historii do pliku.
* Wysyłanie wykrytych sygnałów do SDR Patrol (format jest już zdefiniowany).
* Alarmy (sygnał powyżej progu w zadanym paśmie), maski graniczne.
