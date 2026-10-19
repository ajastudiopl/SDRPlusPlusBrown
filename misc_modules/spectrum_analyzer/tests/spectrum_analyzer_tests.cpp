// Unit / integration tests of the Spectrum Analyzer engine.
// They use synthetic signals and a fake HackRF provider: passing them does NOT
// prove correct operation on real hardware.
#include "test_framework.h"
#include "test_support.h"
#include "../src/engine/analyzer_settings.h"
#include "../src/engine/block_queue.h"
#include "../src/engine/export.h"
#include "../src/engine/generic_backend.h"
#include "../src/engine/native_backend.h"
#include "../src/engine/sample_ring.h"
#include "../src/engine/sweep_controller.h"
#include "../src/engine/synthetic_tuner.h"
#include "../src/engine/view_model.h"
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace spectrum;
using namespace satest;

namespace {
    std::string tmpPath(const char* name) {
        const char* dir = getenv("TMPDIR");
        std::string d = dir ? dir : "/tmp";
        if (!d.empty() && d.back() != '/') { d += "/"; }
        return d + "sa_test_" + name;
    }

    // Runs one complete generic sweep with captures generated directly from the scene.
    void runSyntheticSweep(SweepEngine& eng, const SweepPlan& plan, SyntheticSource& scene, double fs, int64_t t0, const std::vector<int>& skip = {}) {
        std::vector<cf32> buf(eng.samplesNeeded());
        eng.beginSweep(t0);
        for (auto& seg : plan.segments) {
            if (std::find(skip.begin(), skip.end(), seg.index) != skip.end()) { continue; }
            scene.generate(seg.loHz, fs, (int)buf.size(), 0.0, buf.data());
            eng.processCapture(seg.index, buf.data(), (int)buf.size(), t0 + 1000);
        }
        eng.finishSweep(false, t0 + 1000000);
    }

    EngineSettings basicEngine(int fft, int bins) {
        EngineSettings e;
        e.fftSize = fft;
        e.displayBins = bins;
        e.framesPerCapture = 1;
        e.detector = Detector::PEAK;
        e.waterfallRows = 64;
        e.peaks.minLevelDb = -70;
        e.peaks.aboveNoiseDb = 15;
        e.peaks.minSpacingHz = 50e3;
        return e;
    }

    bool hasPeakNear(const std::vector<DetectedPeak>& peaks, double f, double tol) {
        for (auto& p : peaks) {
            if (std::fabs(p.freqHz - f) <= tol) { return true; }
        }
        return false;
    }
}

// ------------------------------------------------------------------ planner
TEST(planner_generic_covers_range_without_gaps) {
    GenericPlanParams p;
    p.range = { 88e6, 108e6 };
    p.sampleRateHz = 2.4e6;
    p.usableFraction = 0.8;
    p.overlapFraction = 0.55;
    p.dcGuardHz = 3 * 2.4e6 / 1024;
    SweepPlan plan = planGenericSweep(p);
    CHECK(plan.valid());
    CHECK(plan.segments.size() >= 20);
    CoverageReport c = analyzeCoverage(plan);
    CHECK_NEAR(c.coveredAnyHz, 20e6, 1.0);
    CHECK(c.maxGapHz == 0);
    CHECK(c.maxCleanGapHz == 0);
    CHECK(plan.dcCoveredByOverlap);
    // First usable edge at the start, last at the stop.
    CHECK_NEAR(plan.segments.front().loHz - 0.96e6, 88e6, 1.0);
    CHECK_NEAR(plan.segments.back().loHz + 0.96e6, 108e6, 1.0);
    // Evenly spaced, step not above U * (1 - overlap)
    for (size_t i = 1; i < plan.segments.size(); i++) {
        CHECK_NEAR(plan.segments[i].loHz - plan.segments[i - 1].loHz, plan.stepHz, 1e-3);
    }
    CHECK(plan.stepHz <= 1.92e6 * 0.45 + 1e-6);
}

TEST(planner_reports_uncovered_dc_regions) {
    GenericPlanParams p;
    p.range = { 88e6, 108e6 };
    p.sampleRateHz = 2.4e6;
    p.overlapFraction = 0.2;
    p.dcGuardHz = 10e3;
    SweepPlan plan = planGenericSweep(p);
    CHECK(plan.valid());
    CHECK(!plan.dcCoveredByOverlap);
    CHECK(!plan.warnings.empty());
    CoverageReport c = analyzeCoverage(plan);
    CHECK(c.maxGapHz == 0);       // DC bins still provide (low priority) data
    CHECK(c.maxCleanGapHz > 0);
    CHECK(minOverlapForDcCoverage(2.4e6 * 0.8, 10e3) > 0.5);
}

TEST(planner_step_override_is_limited_to_usable_bandwidth) {
    GenericPlanParams p;
    p.range = { 100e6, 110e6 };
    p.sampleRateHz = 2e6;
    p.usableFraction = 0.5;
    p.stepOverrideHz = 5e6; // larger than U = 1 MHz
    SweepPlan plan = planGenericSweep(p);
    CHECK(plan.valid());
    CHECK_NEAR(plan.stepHz, 1e6, 1e-6);
    CHECK(!plan.warnings.empty());
    CHECK(analyzeCoverage(plan).maxGapHz == 0);
}

TEST(planner_direction_down_reverses_order) {
    GenericPlanParams p;
    p.range = { 430e6, 440e6 };
    p.sampleRateHz = 2.4e6;
    p.direction = SweepDirection::DOWN;
    SweepPlan plan = planGenericSweep(p);
    CHECK(plan.valid());
    for (size_t i = 0; i < plan.segments.size(); i++) {
        CHECK(plan.segments[i].index == (int)i);
        if (i > 0) { CHECK(plan.segments[i].loHz < plan.segments[i - 1].loHz); }
    }
}

TEST(planner_narrow_span_uses_offset_tuning) {
    GenericPlanParams p;
    p.range = { 145.0e6, 145.2e6 };
    p.sampleRateHz = 2.4e6;
    p.dcGuardHz = 7e3;
    SweepPlan plan = planGenericSweep(p);
    CHECK(plan.valid());
    CHECK(plan.segments.size() == 1);
    double lo = plan.segments[0].loHz;
    CHECK(lo + 7e3 <= 145.0e6);               // DC region below the range
    CHECK(lo + 0.96e6 >= 145.2e6);             // range inside the usable band
    CHECK(analyzeCoverage(plan).maxCleanGapHz == 0);
}

TEST(planner_rejects_invalid_input) {
    GenericPlanParams p;
    p.range = { 100e6, 90e6 };
    p.sampleRateHz = 2e6;
    CHECK(!planGenericSweep(p).valid());
    p.range = { 100e6, 200e6 };
    p.sampleRateHz = 0;
    CHECK(!planGenericSweep(p).valid());
    p.sampleRateHz = 1e5;
    p.range = { 1e6, 6000e6 };
    SweepPlan big = planGenericSweep(p);
    CHECK(!big.valid());
    CHECK(big.error.find("segments") != std::string::npos);
}

TEST(validate_range_limits) {
    RangeLimits lim;
    lim.minHz = 1e6;
    lim.maxHz = 6e9;
    lim.minSpanHz = 1e3;
    CHECK(validateRange({ 88e6, 108e6 }, lim).empty());
    CHECK(!validateRange({ -5, 108e6 }, lim).empty());
    CHECK(!validateRange({ 108e6, 88e6 }, lim).empty());
    CHECK(!validateRange({ 88e6, 88e6 + 10 }, lim).empty());
    CHECK(!validateRange({ 0.5e6, 10e6 }, lim).empty());
    CHECK(!validateRange({ 5e9, 7e9 }, lim).empty());
    FrequencyRange r = FrequencyRange::fromCenterSpan(435e6, 10e6);
    CHECK_NEAR(r.startHz, 430e6, 1e-6);
    CHECK_NEAR(r.stopHz, 440e6, 1e-6);
    CHECK_NEAR(r.centerHz(), 435e6, 1e-6);
    CHECK_NEAR(r.spanHz(), 10e6, 1e-6);
}

TEST(hackrf_native_plan_fm_band) {
    SweepPlan plan = planHackRFNativeSweep({ 88e6, 108e6 });
    CHECK(plan.valid());
    CHECK(plan.native);
    CHECK(plan.segments.size() == 2);
    CHECK_NEAR(plan.segments[0].loHz, 88e6 + 7.5e6, 1e-6);
    CHECK_NEAR(plan.segments[1].loHz, 93e6 + 7.5e6, 1e-6);
    CoverageReport c = analyzeCoverage(plan);
    CHECK(c.maxGapHz == 0);
    CHECK_NEAR(c.coveredHz, 20e6, 1.0);
}

TEST(hackrf_native_plan_alignment) {
    namespace hf = sweep::hackrf_format;
    uint16_t s, e;
    CHECK(hf::alignRange(100e6, 500e6, s, e) == 20);
    CHECK(s == 100 && e == 500);
    CHECK(hf::alignRange(100.4e6, 120.6e6, s, e) == 2);
    CHECK(s == 100 && e == 140);
    CHECK(hf::alignRange(1e6, 6000e6, s, e) == 300);
    CHECK(hf::alignRange(7000e6, 9000e6, s, e) > 0);
    CHECK(e <= 7250);
    CHECK(hf::alignRange(200e6, 100e6, s, e) == 0);
    SweepPlan wide = planHackRFNativeSweep({ 100e6, 500e6 });
    CHECK(wide.segments.size() == 40);
    CHECK(analyzeCoverage(wide).maxGapHz == 0);
    // Last LO stays below the tunable maximum
    SweepPlan top = planHackRFNativeSweep({ 7100e6, 7250e6 });
    CHECK(top.valid());
    CHECK(top.segments.back().loHz <= 7250e6);
}

TEST(hackrf_block_header_roundtrip) {
    namespace hf = sweep::hackrf_format;
    uint8_t block[16] = { 0 };
    uint64_t f = 0;
    CHECK(!hf::parseHeader(block, f));
    hf::writeHeader(block, 5432100000ull);
    CHECK(hf::parseHeader(block, f));
    CHECK(f == 5432100000ull);
    auto offs = hf::validOffsets();
    CHECK(offs.size() == 2);
    CHECK_NEAR(offs[0].loHz, -7.5e6, 1e-6);
    CHECK_NEAR(offs[0].hiHz, -2.5e6, 1e-6);
    CHECK_NEAR(offs[1].loHz, 2.5e6, 1e-6);
    CHECK_NEAR(offs[1].hiHz, 7.5e6, 1e-6);
}

// ------------------------------------------------------------- processing
TEST(segment_processor_tone_calibration_all_windows) {
    const int n = 1024;
    const double fs = 1.024e6; // 1 kHz bins
    for (int w = 0; w <= 4; w++) {
        SegmentProcessor proc;
        SegmentProcessorConfig cfg;
        cfg.fftSize = n;
        cfg.window = (WindowType)w;
        cfg.sampleRateHz = fs;
        std::string err;
        CHECK(proc.configure(cfg, testFftFactory(), err));
        std::vector<cf32> s(n);
        for (int i = 0; i < n; i++) {
            double ph = 2.0 * M_PI * 100.0 * i / n; // +100 kHz
            s[i] = cf32((float)cos(ph), (float)sin(ph));
        }
        CHECK(proc.process(s.data(), n) == 1);
        const auto& p = proc.power();
        float db = 10.0f * log10f(p[n / 2 + 100]);
        CHECK_MSG(std::fabs(db) < 0.05f, "window %s: %.3f dB", windowName((WindowType)w), db);
        // Far away bins are much lower
        CHECK(10.0f * log10f(std::max(p[n / 2 - 200], 1e-30f)) < -40.0f);
    }
}

TEST(segment_processor_uses_last_samples_and_averages) {
    const int n = 256;
    SegmentProcessor proc;
    SegmentProcessorConfig cfg;
    cfg.fftSize = n;
    cfg.maxFrames = 2;
    cfg.sampleRateHz = 256e3;
    cfg.window = WindowType::HANN;
    std::string err;
    CHECK(proc.configure(cfg, testFftFactory(), err));
    // First 3n samples: strong tone, last 2n: silence -> only silence is analysed.
    std::vector<cf32> s(5 * n, cf32(0, 0));
    for (int i = 0; i < 3 * n; i++) { s[i] = cf32(1.0f, 0.0f); }
    CHECK(proc.process(s.data(), (int)s.size()) == 2);
    float mx = 0;
    for (float v : proc.power()) { mx = std::max(mx, v); }
    CHECK(mx < 1e-12f);
    CHECK(proc.process(s.data(), n - 1) == 0);
    CHECK(proc.samplesNeeded() == 2 * n);
}

TEST(segment_processor_chunks_respect_windows) {
    const int n = 1000; // 1 kHz bins at 1 MHz
    SegmentProcessor proc;
    SegmentProcessorConfig cfg;
    cfg.fftSize = n;
    cfg.sampleRateHz = 1e6;
    std::string err;
    CHECK(proc.configure(cfg, testFftFactory(), err));
    std::vector<cf32> s(n, cf32(0.001f, 0));
    proc.process(s.data(), n);
    std::vector<BinChunk> ch;
    std::vector<ValidWindow> w = { { -400e3, -10e3, PRIORITY_CLEAN }, { -10e3, 10e3, PRIORITY_DC }, { 10e3, 400e3, PRIORITY_CLEAN } };
    proc.chunks(100e6, w, ch);
    CHECK(ch.size() == 3);
    CHECK_NEAR(ch[0].firstFreqHz, 100e6 - 400e3, 1e-3);
    CHECK(ch[0].count == 390);
    CHECK(ch[1].priority == PRIORITY_DC);
    CHECK_NEAR(ch[1].firstFreqHz, 100e6 - 10e3, 1e-3);
    CHECK(ch[1].count == 20);
    CHECK_NEAR(ch[2].firstFreqHz, 100e6 + 10e3, 1e-3);
    CHECK(ch[2].count == 390);
    CHECK_NEAR(proc.binWidthHz(), 1e3, 1e-9);
}

// ----------------------------------------------------------------- assembly
TEST(assembler_places_tones_at_correct_frequencies) {
    SyntheticSource scene(1);
    scene.setNoiseDbfs(-100);
    const double tones[] = { 88.512e6, 95.3e6, 99.99e6, 107.41e6 };
    for (double f : tones) { scene.addEmitter({ f, -30.0f }); }
    GenericPlanParams gp;
    gp.range = { 88e6, 108e6 };
    gp.sampleRateHz = 2.4e6;
    gp.overlapFraction = 0.55;
    gp.dcGuardHz = 3 * 2.4e6 / 2048;
    SweepPlan plan = planGenericSweep(gp);
    SweepEngine eng(testFftFactory());
    std::string err;
    CHECK(eng.configure(plan, basicEngine(2048, 8192), err));
    runSyntheticSweep(eng, plan, scene, 2.4e6, 1000);
    EngineSnapshot snap;
    CHECK(eng.snapshot(snap, false, false));
    CHECK(snap.lastComplete);
    CHECK(snap.current.size() == 8192);
    double dbin = 20e6 / 8192;
    for (double f : tones) {
        CHECK_MSG(hasPeakNear(snap.peaks, f, dbin * 1.5), "tone %.3f MHz not found", f / 1e6);
        float v = traceValueAt(snap.current, snap.startHz, snap.stopHz, f);
        CHECK_MSG(std::fabs(v + 30.0f) < 1.5f, "level at %.3f MHz = %.2f", f / 1e6, v);
    }
    // Noise floor about -100 dBFS/sample spread over FFT bins (well below the tones)
    CHECK(snap.noiseFloorDb < -115.0f);
    for (float x : snap.current) { CHECK(std::isfinite(x)); }
}

TEST(assembler_suppresses_dc_spike_with_overlap) {
    SyntheticSource scene(2);
    scene.setNoiseDbfs(-100);
    scene.setDcSpikeDbfs(-25);
    GenericPlanParams gp;
    gp.range = { 430e6, 440e6 };
    gp.sampleRateHz = 2.4e6;
    gp.dcGuardHz = defaultDcGuardHz(2.4e6, 1024, WindowType::BLACKMAN_HARRIS);
    gp.overlapFraction = minOverlapForDcCoverage(2.4e6 * 0.8, gp.dcGuardHz) + 0.02;
    SweepPlan plan = planGenericSweep(gp);
    CHECK(plan.dcCoveredByOverlap);
    SweepEngine eng(testFftFactory());
    std::string err;
    CHECK(eng.configure(plan, basicEngine(1024, 4096), err));
    runSyntheticSweep(eng, plan, scene, 2.4e6, 1000);
    EngineSnapshot snap;
    eng.snapshot(snap, false, false);
    CHECK(snap.peaks.empty());
    CHECK(snap.peakDb < -90.0f);

    // Without a DC guard the spike shows up at every LO.
    gp.dcGuardHz = 0;
    SweepPlan raw = planGenericSweep(gp);
    CHECK(eng.configure(raw, basicEngine(1024, 4096), err));
    runSyntheticSweep(eng, raw, scene, 2.4e6, 1000);
    eng.snapshot(snap, false, false);
    CHECK(snap.peakDb > -40.0f);
}

TEST(assembler_marks_incomplete_sweeps_and_gaps) {
    SyntheticSource scene(3);
    scene.setNoiseDbfs(-100);
    GenericPlanParams gp;
    gp.range = { 100e6, 120e6 };
    gp.sampleRateHz = 2e6;
    gp.overlapFraction = 0.0;
    SweepPlan plan = planGenericSweep(gp);
    SweepEngine eng(testFftFactory());
    std::string err;
    CHECK(eng.configure(plan, basicEngine(512, 2000), err));
    runSyntheticSweep(eng, plan, scene, 2e6, 1000, { 3, 4 });
    EngineSnapshot snap;
    CHECK(eng.snapshot(snap, false, false));
    CHECK(!snap.lastComplete);
    CHECK(snap.lastSegmentsReceived == snap.lastSegmentsExpected - 2);
    int nans = 0;
    for (float x : snap.current) {
        if (!std::isfinite(x)) { nans++; }
    }
    CHECK(nans > 100);
    CHECK(snap.stats.sweepsIncomplete == 1);
    CHECK(snap.stats.lastMissingSegments == 2);
    SweepFrame f;
    CHECK(eng.lastFrame(f));
    CHECK(!f.complete && !f.interrupted);
}

TEST(assembler_interrupted_sweep_is_not_published) {
    SyntheticSource scene(4);
    GenericPlanParams gp;
    gp.range = { 100e6, 110e6 };
    gp.sampleRateHz = 2e6;
    SweepPlan plan = planGenericSweep(gp);
    SweepEngine eng(testFftFactory());
    std::string err;
    CHECK(eng.configure(plan, basicEngine(512, 1024), err));
    uint64_t v0 = eng.version();
    std::vector<cf32> buf(eng.samplesNeeded());
    eng.beginSweep(1);
    scene.generate(plan.segments[0].loHz, 2e6, (int)buf.size(), 0, buf.data());
    eng.processCapture(0, buf.data(), (int)buf.size(), 2);
    EngineSnapshot snap;
    eng.snapshot(snap, true, false);
    CHECK(!snap.partial.empty()); // partial data available (labelled as such in the UI)
    eng.finishSweep(true, 3);
    CHECK(eng.version() == v0);
    CHECK(!eng.snapshot(snap, false, false));
    CHECK(snap.stats.sweepsInterrupted == 1);
    CHECK(eng.waterfall().rows() == 0);
    CHECK(!eng.processCapture(999, buf.data(), (int)buf.size(), 4));
    CHECK(eng.stats().capturesOutsidePlan == 1);
}

TEST(assembler_detector_peak_vs_average) {
    // Two FFT bins with different power falling in one display bin.
    SpectrumAssembler a;
    a.configure(0, 1000, 10, Detector::PEAK, 1);
    a.beginSweep(0);
    float p[2] = { 1.0f, 0.01f };
    BinChunk c;
    c.firstFreqHz = 20;
    c.binWidthHz = 10;
    c.count = 2;
    c.power = p;
    a.addChunk(c);
    a.markSegmentReceived(0);
    SweepFrame f;
    a.finishSweep(f, false, 1);
    CHECK_NEAR(f.db[0], 0.0, 1e-4);
    CHECK(f.complete);
    a.configure(0, 1000, 10, Detector::AVERAGE, 1);
    a.beginSweep(0);
    a.addChunk(c);
    a.finishSweep(f, false, 1);
    CHECK_NEAR(f.db[0], 10.0 * log10(0.505), 1e-3);
    CHECK(!f.complete); // segment 0 never marked
    // Higher priority replaces lower priority data
    a.configure(0, 1000, 10, Detector::PEAK, 1);
    a.beginSweep(0);
    float dc[1] = { 100.0f };
    BinChunk d;
    d.firstFreqHz = 50;
    d.binWidthHz = 10;
    d.count = 1;
    d.power = dc;
    d.priority = PRIORITY_DC;
    a.addChunk(d);
    float clean[1] = { 0.001f };
    BinChunk e = d;
    e.power = clean;
    e.priority = PRIORITY_CLEAN;
    a.addChunk(e);
    a.finishSweep(f, false, 1);
    CHECK_NEAR(f.db[0], -30.0, 1e-3);
    CHECK(!std::isfinite(f.db[5]));
}

TEST(assembler_fills_fine_display_grid) {
    SpectrumAssembler a;
    a.configure(0, 100, 100, Detector::PEAK, 1); // 1 Hz display bins
    a.beginSweep(0);
    float p[2] = { 1.0f, 0.1f };
    BinChunk c;
    c.firstFreqHz = 25;
    c.binWidthHz = 10; // covers 20..30 and 30..40
    c.count = 2;
    c.power = p;
    a.addChunk(c);
    SweepFrame f;
    a.finishSweep(f, false, 1);
    for (int i = 20; i < 30; i++) { CHECK_NEAR(f.db[i], 0.0, 1e-4); }
    for (int i = 30; i < 40; i++) { CHECK_NEAR(f.db[i], -10.0, 1e-4); }
    CHECK(!std::isfinite(f.db[19]));
    CHECK(!std::isfinite(f.db[40]));
}

// --------------------------------------------------------------- native flow
TEST(native_blocks_assemble_full_cycles_only) {
    namespace hf = sweep::hackrf_format;
    SyntheticSource scene(5);
    scene.setNoiseDbfs(-90);
    scene.addEmitter({ 433.92e6, -35.0f });
    SweepPlan plan = planHackRFNativeSweep({ 400e6, 440e6 });
    CHECK(plan.segments.size() == 4);
    SweepEngine eng(testFftFactory());
    std::string err;
    CHECK(eng.configure(plan, basicEngine(1024, 4096), err));
    std::vector<cf32> buf(hf::BLOCK_SAMPLES);
    std::vector<double> freqs = hf::interleavedBlockFrequencies(400, 2);
    int64_t t = 1000;
    auto feed = [&](double f) {
        scene.generate(f + hf::OFFSET_HZ, hf::SAMPLE_RATE_HZ, (int)buf.size(), 0, buf.data());
        eng.processNativeBlock((uint64_t)f, hf::OFFSET_HZ, buf.data(), (int)buf.size(), t);
        t += 1000000;
    };
    // Start in the middle of a sweep: these blocks must be ignored.
    feed(freqs[2]);
    feed(freqs[3]);
    CHECK(eng.stats().capturesProcessed == 0);
    for (int sweepNo = 0; sweepNo < 3; sweepNo++) {
        for (double f : freqs) { feed(f); }
    }
    feed(freqs[0]); // closes the third sweep
    EngineStats st = eng.stats();
    CHECK(st.sweepsCompleted == 3);
    CHECK(st.sweepsIncomplete == 0);
    EngineSnapshot snap;
    eng.snapshot(snap, false, false);
    CHECK(snap.lastComplete);
    CHECK(hasPeakNear(snap.peaks, 433.92e6, 40e6 / 4096 * 1.5));
    CHECK(eng.waterfall().rows() == 3);
    // Blocks of an unknown tuning are counted, not mapped somewhere wrong.
    feed(1000e6);
    CHECK(eng.stats().capturesOutsidePlan == 1);
}

TEST(native_dropped_blocks_make_incomplete_frames) {
    namespace hf = sweep::hackrf_format;
    SyntheticSource scene(6);
    SweepPlan plan = planHackRFNativeSweep({ 400e6, 440e6 });
    SweepEngine eng(testFftFactory());
    std::string err;
    CHECK(eng.configure(plan, basicEngine(1024, 4096), err));
    std::vector<cf32> buf(hf::BLOCK_SAMPLES);
    std::vector<double> freqs = hf::interleavedBlockFrequencies(400, 2);
    int64_t t = 1;
    auto feed = [&](double f) {
        scene.generate(f + hf::OFFSET_HZ, hf::SAMPLE_RATE_HZ, (int)buf.size(), 0, buf.data());
        eng.processNativeBlock((uint64_t)f, hf::OFFSET_HZ, buf.data(), (int)buf.size(), t);
        t += 1000;
    };
    // sweep 1 complete, sweep 2 misses block 2, sweep 3 misses block 0 (not started)
    for (double f : freqs) { feed(f); }
    feed(freqs[0]);
    feed(freqs[1]);
    feed(freqs[3]);
    feed(freqs[1]);
    feed(freqs[2]);
    feed(freqs[3]);
    feed(freqs[0]);
    EngineStats st = eng.stats();
    CHECK(st.sweepsCompleted == 1);
    CHECK(st.sweepsIncomplete == 1);
    CHECK(st.lastMissingSegments == 1);
    CHECK(eng.waterfall().rows() == 2);
    {
        auto lck = eng.waterfall().lock();
        WaterfallRowInfo info;
        eng.waterfall().rowByAge(0, &info);
        CHECK(!info.complete);
        eng.waterfall().rowByAge(1, &info);
        CHECK(info.complete);
    }
}

TEST(refresh_limit_holds_next_sweep) {
    namespace hf = sweep::hackrf_format;
    SyntheticSource scene(7);
    SweepPlan plan = planHackRFNativeSweep({ 400e6, 420e6 });
    SweepEngine eng(testFftFactory());
    EngineSettings e = basicEngine(512, 1024);
    e.refreshLimitHz = 2.0; // one sweep per 500 ms
    std::string err;
    CHECK(eng.configure(plan, e, err));
    std::vector<cf32> buf(hf::BLOCK_SAMPLES);
    std::vector<double> freqs = hf::interleavedBlockFrequencies(400, 1);
    int64_t t = 1000000000;
    auto sweepAt = [&](int64_t ts) {
        for (double f : freqs) {
            scene.generate(f + hf::OFFSET_HZ, hf::SAMPLE_RATE_HZ, (int)buf.size(), 0, buf.data());
            eng.processNativeBlock((uint64_t)f, hf::OFFSET_HZ, buf.data(), (int)buf.size(), ts);
        }
    };
    sweepAt(t);
    sweepAt(t + 10000000);   // closes sweep 1, sweep 2 is not allowed yet
    sweepAt(t + 20000000);
    CHECK(eng.stats().sweepsCompleted == 1);
    CHECK(!eng.sweepAllowed(t + 100000000));
    CHECK(eng.sweepAllowed(t + 600000000));
    sweepAt(t + 600000000);
    sweepAt(t + 610000000);
    CHECK(eng.stats().sweepsCompleted == 2);
}

// ------------------------------------------------------------------ waterfall
TEST(waterfall_ring_buffer_order_and_capacity) {
    WaterfallHistory h;
    h.configure(8, 4);
    std::vector<float> row(8);
    for (int r = 0; r < 6; r++) {
        std::fill(row.begin(), row.end(), -100.0f + 10.0f * r);
        WaterfallRowInfo info;
        info.sequence = r;
        info.startHz = 1e6;
        info.stopHz = 2e6;
        h.push(row.data(), 8, info);
    }
    auto lck = h.lock();
    CHECK(h.rows() == 4);
    CHECK(h.totalPushed() == 6);
    WaterfallRowInfo info;
    const uint8_t* newest = h.rowByAge(0, &info);
    CHECK(info.sequence == 5);
    CHECK_NEAR(WaterfallHistory::dequantize(newest[0]), -50.0, 0.5);
    h.rowByAge(3, &info);
    CHECK(info.sequence == 2);
    CHECK(h.rowByAge(4) == nullptr);
    CHECK_NEAR(info.startHz, 1e6, 1e-9);
    CHECK_NEAR(info.stopHz, 2e6, 1e-9);
}

TEST(waterfall_memory_is_bounded_and_clear_works) {
    WaterfallHistory h;
    h.configure(100000, 4096, 1 << 20);
    {
        auto lck = h.lock();
        CHECK(h.capacity() <= 10);
        CHECK(h.memoryBytes() <= (1u << 20) + 4096 * sizeof(WaterfallRowInfo));
    }
    uint64_t g = h.generation();
    std::vector<float> row(100000, -80.0f);
    h.push(row.data(), 100000, WaterfallRowInfo());
    h.push(row.data(), 5, WaterfallRowInfo()); // wrong width is ignored
    {
        auto lck = h.lock();
        CHECK(h.rows() == 1);
    }
    h.clear();
    auto lck = h.lock();
    CHECK(h.rows() == 0);
    CHECK(h.generation() != g);
}

TEST(waterfall_quantization) {
    for (float db = -159.0f; db < 39.0f; db += 0.37f) {
        CHECK(std::fabs(WaterfallHistory::dequantize(WaterfallHistory::quantize(db)) - db) <= 0.4f);
    }
    CHECK(WaterfallHistory::quantize(nanValue()) == WaterfallHistory::QUANT_NAN);
    CHECK(!std::isfinite(WaterfallHistory::dequantize(WaterfallHistory::QUANT_NAN)));
    CHECK(WaterfallHistory::quantize(-500.0f) == 0);
    CHECK(WaterfallHistory::quantize(500.0f) == 254);
}

TEST(waterfall_rows_follow_sweeps_per_line_and_stop_resume) {
    SyntheticSource scene(8);
    GenericPlanParams gp;
    gp.range = { 100e6, 104e6 };
    gp.sampleRateHz = 2e6;
    SweepPlan plan = planGenericSweep(gp);
    SweepEngine eng(testFftFactory());
    EngineSettings e = basicEngine(256, 512);
    e.sweepsPerLine = 3;
    std::string err;
    CHECK(eng.configure(plan, e, err));
    for (int i = 0; i < 7; i++) { runSyntheticSweep(eng, plan, scene, 2e6, 1000 + i * 1000000); }
    CHECK(eng.waterfall().rows() == 2);
    // stop in the middle of a sweep, then resume
    std::vector<cf32> buf(eng.samplesNeeded());
    eng.beginSweep(1);
    scene.generate(plan.segments[0].loHz, 2e6, (int)buf.size(), 0, buf.data());
    eng.processCapture(0, buf.data(), (int)buf.size(), 2);
    eng.abortSweep();
    CHECK(eng.waterfall().rows() == 2);
    for (int i = 0; i < 2; i++) { runSyntheticSweep(eng, plan, scene, 2e6, 9000000 + i * 1000000); }
    CHECK(eng.waterfall().rows() == 3);
    eng.clearWaterfall();
    CHECK(eng.waterfall().rows() == 0);
    // frequency axis of the rows equals the swept range
    runSyntheticSweep(eng, plan, scene, 2e6, 20000000);
    runSyntheticSweep(eng, plan, scene, 2e6, 21000000);
    runSyntheticSweep(eng, plan, scene, 2e6, 22000000);
    auto lck = eng.waterfall().lock();
    WaterfallRowInfo info;
    CHECK(eng.waterfall().rowByAge(0, &info) != nullptr);
    CHECK_NEAR(info.startHz, 100e6, 1e-6);
    CHECK_NEAR(info.stopHz, 104e6, 1e-6);
    CHECK(eng.waterfall().width() == 512);
}

// -------------------------------------------------------------- display traces
TEST(display_average_maxhold_persistence) {
    DisplayProcessor d;
    DisplaySettings s;
    s.averageCount = 4;
    s.persistence = 3;
    s.peakDecayDbPerSec = 10.0f;
    d.setSettings(s);
    d.reset(4);
    SweepFrame f;
    f.db = { -100, -100, -50, nanValue() };
    for (int i = 0; i < 10; i++) {
        f.endTimeNs = (int64_t)(i + 1) * 1000000000;
        f.db[0] = (i == 2) ? -20.0f : -100.0f;
        d.push(f);
    }
    CHECK((int)d.persistenceTraces().size() == 3);
    CHECK_NEAR(d.average()[1], -100.0, 0.01);
    CHECK(!std::isfinite(d.average()[3]));
    // max hold at bin 0 decayed 10 dB/s for 7 s after the -20 dB sweep
    CHECK_NEAR(d.maxHold()[0], -90.0, 0.01);
    CHECK_NEAR(d.maxHold()[2], -50.0, 0.01);
    d.resetMaxHold();
    CHECK(!std::isfinite(d.maxHold()[2]));
    std::vector<float> tmp;
    std::vector<float> v = { 5, 1, 4, 2, 3, nanValue() };
    CHECK_NEAR(DisplayProcessor::percentile(v, 0.0f, tmp), 1.0, 1e-6);
    CHECK_NEAR(DisplayProcessor::percentile(v, 0.5f, tmp), 3.0, 1e-6);
}

// ------------------------------------------------------------------- peaks
TEST(peak_detector_threshold_spacing_width) {
    std::vector<float> db(1000, -100.0f);
    db[100] = -40.0f;                                   // narrow
    for (int i = 300; i < 320; i++) { db[i] = -50.0f; } // wide (20 bins)
    db[310] = -45.0f;
    db[105] = -60.0f;                                   // too close to bin 100
    db[700] = -95.0f;                                   // below threshold
    db[900] = nanValue();
    PeakDetectorConfig cfg;
    cfg.minLevelDb = -90.0f;
    cfg.aboveNoiseDb = 10.0f;
    cfg.minSpacingHz = 10e3;
    auto peaks = detectPeaks(db, 0.0, 1e3, -100.0f, cfg);
    CHECK(peaks.size() == 2);
    CHECK_NEAR(peaks[0].freqHz, 100.5e3, 1e-6);
    CHECK_NEAR(peaks[0].aboveNoiseDb, 60.0, 1e-4);
    CHECK_NEAR(peaks[1].freqHz, 310.5e3, 1e-6);
    CHECK_NEAR(peaks[1].widthHz, 20e3, 1e-6);
    cfg.minWidthHz = 5e3;
    peaks = detectPeaks(db, 0.0, 1e3, -100.0f, cfg);
    CHECK(peaks.size() == 1);
    CHECK_NEAR(peaks[0].freqHz, 310.5e3, 1e-6);
    cfg.minWidthHz = 0;
    cfg.maxPeaks = 1;
    peaks = detectPeaks(db, 0.0, 1e3, -100.0f, cfg);
    CHECK(peaks.size() == 1);
    CHECK_NEAR(peaks[0].levelDb, -40.0, 1e-6);
}

// --------------------------------------------------------- settings / presets
TEST(settings_json_roundtrip_and_validation) {
    AnalyzerSettings s;
    s.range = { 430e6, 440e6 };
    s.sweep.mode = SweepMode::GENERIC;
    s.sweep.fftSize = 4096;
    s.sweep.window = WindowType::NUTTALL;
    s.sweep.detector = Detector::AVERAGE;
    s.sweep.overlapFraction = 0.6;
    s.sweep.settleMs = 33;
    s.display.minDb = -120;
    s.display.maxDb = -20;
    s.display.proc.averageCount = 16;
    s.waterfall.palette = "Viridis";
    s.waterfall.historyRows = 900;
    s.gains["lna"] = 24;
    s.peaks.minSpacingHz = 12500;
    nlohmann::json j = settingsToJson(s);
    AnalyzerSettings r;
    settingsFromJson(j, r);
    CHECK_NEAR(r.range.startHz, 430e6, 1e-6);
    CHECK(r.sweep.mode == SweepMode::GENERIC);
    CHECK(r.sweep.fftSize == 4096);
    CHECK(r.sweep.window == WindowType::NUTTALL);
    CHECK(r.sweep.detector == Detector::AVERAGE);
    CHECK_NEAR(r.sweep.overlapFraction, 0.6, 1e-9);
    CHECK_NEAR(r.sweep.settleMs, 33, 1e-9);
    CHECK_NEAR(r.display.minDb, -120, 1e-6);
    CHECK(r.display.proc.averageCount == 16);
    CHECK(r.waterfall.palette == "Viridis");
    CHECK(r.waterfall.historyRows == 900);
    CHECK_NEAR(r.gains["lna"], 24, 1e-9);
    CHECK_NEAR(r.peaks.minSpacingHz, 12500, 1e-9);

    // Broken / hostile values are rejected or clamped
    nlohmann::json bad = j;
    bad["sweep"]["fftSize"] = 1000;
    bad["sweep"]["overlapFraction"] = 3.0;
    bad["sweep"]["mode"] = 17;
    bad["sweep"]["window"] = "hann";
    bad["range"]["stop"] = 1.0;
    bad["display"]["minDb"] = 10;
    bad["display"]["maxDb"] = -10;
    bad["waterfall"]["historyRows"] = 1000000;
    AnalyzerSettings b;
    settingsFromJson(bad, b);
    CHECK(b.sweep.fftSize == 2048);
    CHECK_NEAR(b.sweep.overlapFraction, 0.9, 1e-9);
    CHECK(b.sweep.mode == SweepMode::AUTO);
    CHECK(b.sweep.window == WindowType::BLACKMAN_HARRIS);
    CHECK(b.range.stopHz > b.range.startHz);
    CHECK(b.display.maxDb > b.display.minDb);
    CHECK(b.waterfall.historyRows == 2048);
    settingsFromJson(nlohmann::json::array(), b); // not an object: ignored
}

TEST(presets_and_markers) {
    AnalyzerSettings base;
    auto presets = builtInPresets(base);
    CHECK(presets.size() >= 6);
    bool fm = false, air = false;
    for (auto& p : presets) {
        CHECK(p.builtIn);
        CHECK(p.settings.range.stopHz > p.settings.range.startHz);
        if (p.name.find("FM") != std::string::npos) { fm = std::fabs(p.settings.range.startHz - 88e6) < 1; }
        if (p.name.find("Airband") != std::string::npos) { air = std::fabs(p.settings.range.stopHz - 137e6) < 1; }
    }
    CHECK(fm && air);
    std::vector<Marker> m = { { "Tower", 145.5e6 }, { "", 433.92e6 } };
    auto back = markersFromJson(markersToJson(m));
    CHECK(back.size() == 2);
    CHECK(back[0].name == "Tower");
    CHECK_NEAR(back[1].freqHz, 433.92e6, 1e-6);
    nlohmann::json junk = nlohmann::json::array({ { { "freq", -5 } }, { { "name", "x" } } });
    CHECK(markersFromJson(junk).size() == 1); // freq defaults to 0 -> kept, negative dropped
}

// ------------------------------------------------------------------- export
TEST(export_csv_contains_measured_bins) {
    EngineSnapshot snap;
    snap.startHz = 100e6;
    snap.stopHz = 101e6;
    snap.current = { -80.0f, nanValue(), -60.5f, -70.0f };
    snap.average = { -81.0f, -82.0f, -61.0f, -71.0f };
    snap.maxHold = { -79.0f, -78.0f, -55.0f, -65.0f };
    snap.lastComplete = true;
    MeasurementMetadata meta;
    meta.device = "Test";
    meta.backend = "Demo (synthetic)";
    meta.synthetic = true;
    meta.unixTimeMs = 1760000000000;
    std::string err;
    std::string path = tmpPath("export.csv");
    CHECK(writeSpectrumCsv(path, meta, snap, err));
    std::ifstream f(path);
    std::string line;
    int data = 0;
    bool sawSynthetic = false;
    std::vector<std::string> rows;
    while (std::getline(f, line)) {
        if (line.rfind("#", 0) == 0) {
            if (line.find("SYNTHETIC") != std::string::npos) { sawSynthetic = true; }
            continue;
        }
        if (line.rfind("frequency_hz", 0) == 0) { continue; }
        rows.push_back(line);
        data++;
    }
    CHECK(sawSynthetic);
    CHECK(data == 4);
    CHECK(rows[0] == "100125000.0,-80.00,-81.00,-79.00");
    CHECK(rows[1] == "100375000.0,,-82.00,-78.00");
    std::remove(path.c_str());
    EngineSnapshot empty;
    CHECK(!writeSpectrumCsv(path, meta, empty, err));
}

TEST(export_png_is_valid) {
    const int w = 3, h = 2;
    uint8_t px[w * h * 4];
    for (int i = 0; i < w * h * 4; i++) { px[i] = (uint8_t)(i * 7); }
    std::vector<uint8_t> png = encodePngRgba(w, h, px);
    const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    CHECK(std::equal(sig, sig + 8, png.begin()));
    CHECK(std::string((const char*)&png[12], 4) == "IHDR");
    CHECK(png[16 + 3] == w && png[20 + 3] == h && png[24] == 8 && png[25] == 6);
    // Find IDAT and decode the stored deflate blocks
    size_t pos = 8;
    std::vector<uint8_t> raw;
    bool sawEnd = false;
    while (pos + 8 <= png.size()) {
        uint32_t len = (png[pos] << 24) | (png[pos + 1] << 16) | (png[pos + 2] << 8) | png[pos + 3];
        std::string type((const char*)&png[pos + 4], 4);
        const uint8_t* d = &png[pos + 8];
        if (type == "IDAT") {
            CHECK(d[0] == 0x78);
            size_t p = 2;
            while (true) {
                uint8_t hdr = d[p];
                uint16_t l = d[p + 1] | (d[p + 2] << 8);
                uint16_t nl = d[p + 3] | (d[p + 4] << 8);
                CHECK((uint16_t)~l == nl);
                raw.insert(raw.end(), d + p + 5, d + p + 5 + l);
                p += 5 + l;
                if (hdr & 1) { break; }
            }
        }
        if (type == "IEND") { sawEnd = true; }
        pos += 12 + len;
    }
    CHECK(sawEnd);
    CHECK(raw.size() == (size_t)h * (w * 4 + 1));
    CHECK(raw[0] == 0);
    CHECK(std::equal(px, px + w * 4, raw.begin() + 1));
    CHECK(std::equal(px + w * 4, px + 2 * w * 4, raw.begin() + w * 4 + 2));
    std::string err;
    std::string path = tmpPath("export.png");
    CHECK(writePngRgba(path, w, h, px, err));
    std::remove(path.c_str());
    CHECK(!writePngRgba(path, 0, 0, px, err));
}

TEST(export_json_signal_reports_for_sdr_patrol) {
    std::vector<DetectedPeak> peaks(2);
    peaks[0].freqHz = 145.5e6;
    peaks[0].levelDb = -40;
    peaks[0].aboveNoiseDb = 50;
    peaks[0].widthHz = 12e3;
    peaks[1].freqHz = 433.92e6;
    peaks[1].levelDb = -55;
    std::vector<Marker> markers = { { "Repeater", 145.5005e6 }, { "Far", 400e6 } };
    MeasurementMetadata meta;
    meta.device = "HackRF One 0000abcd";
    meta.backend = "HackRF native sweep";
    meta.rbwHz = 19.5e3;
    meta.fftSize = 1024;
    meta.detector = "peak";
    meta.sampleRateHz = 20e6;
    meta.unixTimeMs = 1760000000123;
    auto reports = buildSignalReports(peaks, markers, meta, 100e6, 500e6, 5e3);
    CHECK(reports.size() == 2);
    CHECK(reports[0].markerName == "Repeater");
    CHECK(reports[1].markerName.empty());
    nlohmann::json doc = signalsDocument(reports, meta);
    CHECK(doc["schema"] == SIGNALS_SCHEMA);
    CHECK(doc["signals"].size() == 2);
    auto& s0 = doc["signals"][0];
    CHECK(s0["frequency_hz"].get<double>() == 145.5e6);
    CHECK(s0["range"]["start_hz"].get<double>() == 100e6);
    CHECK(s0["device"] == "HackRF One 0000abcd");
    CHECK(s0["sweep"]["fft_size"] == 1024);
    CHECK(s0["marker"] == "Repeater");
    CHECK(doc["signals"][1]["marker"].is_null());
    CHECK(s0["timestamp"] == "2025-10-09T08:53:20.123Z");
    EngineSnapshot snap;
    snap.startHz = 100e6;
    snap.stopHz = 500e6;
    nlohmann::json sd = snapshotDocument(meta, snap, markers, reports, "a.csv", "a.png");
    CHECK(sd["schema"] == SNAPSHOT_SCHEMA);
    CHECK(sd["markers"].size() == 2);
    CHECK(sd["files"]["csv"] == "a.csv");
}

// ------------------------------------------------------------ queues / rings
TEST(block_queue_drops_when_full_and_keeps_last_samples) {
    BlockQueue q;
    q.configure(2, 4);
    float iq[12];
    for (int i = 0; i < 12; i++) { iq[i] = (float)i; }
    CHECK(q.push(1, 10, iq, 6));
    CHECK(q.push(2, 20, iq, 6));
    CHECK(!q.push(3, 30, iq, 6));
    CHECK(q.droppedCount() == 1);
    BlockQueue::Block b;
    CHECK(q.pop(b, 10));
    CHECK(b.freqHz == 1 && b.count == 4);
    CHECK(b.samples[0] == cf32(4, 5)); // last 4 of 6 samples
    q.release();
    CHECK(q.pop(b, 10));
    CHECK(b.freqHz == 2);
    q.release();
    CHECK(!q.pop(b, 10));
    CHECK(q.push(4, 40, iq, 2));
    q.drain();
    CHECK(q.size() == 0);
}

TEST(sample_ring_positions_and_overwrite) {
    SampleRing r;
    r.configure(16);
    std::vector<cf32> s(40);
    for (int i = 0; i < 40; i++) { s[i] = cf32((float)i, 0); }
    r.write(s.data(), 10);
    CHECK(r.written() == 10);
    std::vector<cf32> out(4);
    CHECK(r.copy(2, 4, out.data()));
    CHECK(out[0].real() == 2);
    CHECK(!r.copy(8, 4, out.data())); // not enough samples yet
    r.write(s.data() + 10, 30);       // 40 written, ring holds 24..39
    CHECK(r.copy(30, 4, out.data()));
    CHECK(out[0].real() == 30);
    CHECK(r.copy(5, 4, out.data())); // overwritten -> newest samples
    CHECK(out[0].real() == 36);
    CHECK(!r.waitFor(100, 10));
    r.close();
    CHECK(r.closed());
}

// ----------------------------------------------------- device management
namespace {
    BackendStartParams genericParams(double start, double stop) {
        BackendStartParams p;
        p.range = { start, stop };
        p.sweep.autoParams = true;
        p.sweep.settleMs = 1;
        p.engine = basicEngine(1024, 2048);
        return p;
    }

    std::unique_ptr<GenericSweepBackend> syntheticBackend(SyntheticTuner** out = nullptr, double fs = 2.4e6) {
        SyntheticSource scene(11);
        scene.setNoiseDbfs(-90);
        scene.addEmitter({ 100.3e6, -40.0f });
        SyntheticTunerConfig cfg;
        cfg.sampleRateHz = fs;
        auto t = std::make_unique<SyntheticTuner>(scene, cfg);
        if (out) { *out = t.get(); }
        return std::make_unique<GenericSweepBackend>(std::move(t), "synthetic");
    }
}

TEST(controller_start_stop_and_repeated_cycles) {
    SweepEngine eng(testFftFactory());
    SweepController ctl(eng);
    CHECK(ctl.state() == SweepState::IDLE);
    std::string err;
    CHECK(ctl.start(syntheticBackend(), genericParams(99e6, 102e6), err));
    CHECK(!ctl.start(syntheticBackend(), genericParams(99e6, 102e6), err)); // already running
    CHECK(waitUntil([&] { return eng.stats().sweepsCompleted >= 2; }, 10000));
    CHECK(ctl.state() == SweepState::SWEEPING);
    EngineSnapshot snap;
    eng.snapshot(snap, false, false);
    CHECK(hasPeakNear(snap.peaks, 100.3e6, 3e6 / 2048 * 1.5));
    ctl.requestStop();
    CHECK(waitUntil([&] { ctl.update(); return ctl.state() == SweepState::IDLE && ctl.backend() == nullptr; }, 5000));
    for (int i = 0; i < 5; i++) {
        CHECK(ctl.start(syntheticBackend(), genericParams(99e6, 102e6), err));
        std::this_thread::sleep_for(std::chrono::milliseconds(i * 3));
        ctl.stopAndWait();
        CHECK(ctl.state() == SweepState::IDLE);
    }
    CHECK(ctl.runCount() == 6);
}

TEST(controller_pause_and_resume) {
    SweepEngine eng(testFftFactory());
    SweepController ctl(eng);
    std::string err;
    CHECK(ctl.start(syntheticBackend(), genericParams(99e6, 102e6), err));
    CHECK(waitUntil([&] { return eng.stats().sweepsCompleted >= 1; }, 10000));
    ctl.setPaused(true);
    CHECK(ctl.state() == SweepState::PAUSED);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    uint64_t done = eng.stats().sweepsCompleted;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(eng.stats().sweepsCompleted == done);
    ctl.setPaused(false);
    CHECK(ctl.state() == SweepState::SWEEPING);
    CHECK(waitUntil([&] { return eng.stats().sweepsCompleted > done; }, 10000));
    ctl.stopAndWait();
}

TEST(controller_reports_disconnect_and_recovers) {
    SweepEngine eng(testFftFactory());
    SweepController ctl(eng);
    SyntheticTuner* tuner = nullptr;
    std::string err;
    CHECK(ctl.start(syntheticBackend(&tuner), genericParams(99e6, 102e6), err));
    CHECK(waitUntil([&] { return eng.stats().sweepsCompleted >= 1; }, 10000));
    tuner->simulateDisconnect();
    CHECK(waitUntil([&] { ctl.update(); return ctl.state() == SweepState::DEVICE_DISCONNECTED; }, 10000));
    CHECK(ctl.message().find("disconnected") != std::string::npos);
    ctl.update();
    // "re-plugged": a new device works again
    CHECK(ctl.start(syntheticBackend(), genericParams(99e6, 102e6), err));
    CHECK(waitUntil([&] { return ctl.state() == SweepState::SWEEPING; }, 5000));
    ctl.stopAndWait();
    CHECK(ctl.state() == SweepState::IDLE);
}

TEST(controller_generic_without_sample_rate_fails_cleanly) {
    SweepEngine eng(testFftFactory());
    SweepController ctl(eng);
    std::string err;
    CHECK(ctl.start(syntheticBackend(nullptr, 0.0), genericParams(99e6, 102e6), err));
    CHECK(waitUntil([&] { ctl.update(); return ctl.state() == SweepState::FAULT; }, 5000));
    CHECK(!ctl.message().empty());
}

TEST(native_backend_conflict_no_device_and_unplug) {
    SyntheticSource scene(12);
    scene.setNoiseDbfs(-90);
    scene.addEmitter({ 433.92e6, -40.0f });
    FakeHackRFProvider prov(scene);
    SweepEngine eng(testFftFactory());
    SweepController ctl(eng);
    BackendStartParams p;
    p.range = { 400e6, 440e6 };
    p.engine = basicEngine(1024, 4096);
    p.gains["lna"] = 24;
    std::string err;

    // Receiver owns the device -> clear refusal
    prov.receiverRunning = true;
    CHECK(ctl.start(std::make_unique<NativeSweepBackend>(&prov, "fake0", "Fake HackRF"), p, err));
    CHECK(waitUntil([&] { ctl.update(); return ctl.state() == SweepState::FAULT; }, 5000));
    CHECK(ctl.message().find("in use by the receiver") != std::string::npos);
    prov.receiverRunning = false;

    // No device
    prov.present = false;
    CHECK(ctl.start(std::make_unique<NativeSweepBackend>(&prov, "fake0", "Fake HackRF"), p, err));
    CHECK(waitUntil([&] { ctl.update(); return ctl.state() == SweepState::FAULT || ctl.state() == SweepState::DEVICE_DISCONNECTED; }, 5000));
    prov.present = true;

    // Old firmware
    prov.firmwareOk = false;
    CHECK(ctl.start(std::make_unique<NativeSweepBackend>(&prov, "fake0", "Fake HackRF"), p, err));
    CHECK(waitUntil([&] { ctl.update(); return ctl.state() == SweepState::FAULT; }, 5000));
    CHECK(ctl.message().find("Firmware") != std::string::npos);
    prov.firmwareOk = true;

    // Works, gains are forwarded, peaks found
    CHECK(ctl.start(std::make_unique<NativeSweepBackend>(&prov, "fake0", "Fake HackRF"), p, err));
    CHECK(waitUntil([&] { return eng.stats().sweepsCompleted >= 3; }, 15000));
    CHECK(prov.getOwnership("fake0") == sweep::Ownership::SWEEP);
    CHECK(ctl.backend()->setGain("vga", 30));
    CHECK(prov.lastGainId == "vga");
    EngineSnapshot snap;
    eng.snapshot(snap, false, false);
    CHECK(hasPeakNear(snap.peaks, 433.92e6, 40e6 / 4096 * 1.5));
    CHECK(snap.stats.sweepsPerSec > 0);

    // Unplug while sweeping
    prov.unplug();
    CHECK(waitUntil([&] { ctl.update(); return ctl.state() == SweepState::DEVICE_DISCONNECTED; }, 10000));
    CHECK(prov.getOwnership("fake0") == sweep::Ownership::FREE); // released
}

TEST(native_backend_counts_usb_drops_as_incomplete) {
    SyntheticSource scene(13);
    FakeHackRFProvider prov(scene);
    prov.skipEveryNthBlock = 7;
    SweepEngine eng(testFftFactory());
    SweepController ctl(eng);
    BackendStartParams p;
    p.range = { 400e6, 480e6 };
    p.engine = basicEngine(512, 2048);
    std::string err;
    CHECK(ctl.start(std::make_unique<NativeSweepBackend>(&prov, "fake0", "Fake HackRF"), p, err));
    CHECK(waitUntil([&] { return eng.stats().sweepsIncomplete >= 2; }, 15000));
    ctl.stopAndWait();
    CHECK(ctl.state() == SweepState::IDLE);
}

TEST(generic_settling_discards_stale_samples) {
    // The receiver pipeline keeps delivering samples of the previous LO for
    // 15 ms after a retune. Without settling every capture is stale and the
    // carrier at 100 MHz is drawn at a wrong frequency; with 20 ms settling
    // it appears only where it really is.
    struct Result {
        bool atTrueFreq;
        int ghosts;
    };
    auto run = [&](double settleMs) {
        SyntheticSource scene(14);
        scene.setNoiseDbfs(-100);
        scene.addEmitter({ 100.0e6, -20.0f });
        SyntheticTunerConfig cfg;
        cfg.sampleRateHz = 2e6;
        cfg.retuneLatencySamples = 30000; // 15 ms of stale samples
        auto tuner = std::make_unique<SyntheticTuner>(scene, cfg);
        SweepEngine eng(testFftFactory());
        SweepController ctl(eng);
        BackendStartParams p;
        p.range = { 98e6, 104e6 };
        p.sweep.autoParams = false;
        p.sweep.overlapFraction = 0.0;
        p.sweep.settleMs = settleMs;
        p.engine = basicEngine(1024, 3000);
        p.engine.peaks.minLevelDb = -60;
        std::string err;
        CHECK(ctl.start(std::make_unique<GenericSweepBackend>(std::move(tuner), "lag"), p, err));
        CHECK(waitUntil([&] { return eng.stats().sweepsCompleted >= 2; }, 20000));
        ctl.stopAndWait();
        EngineSnapshot snap;
        eng.snapshot(snap, false, false);
        Result r{ hasPeakNear(snap.peaks, 100.0e6, 6e6 / 3000 * 2), 0 };
        for (auto& pk : snap.peaks) {
            if (std::fabs(pk.freqHz - 100.0e6) > 20e3) { r.ghosts++; }
        }
        return r;
    };
    Result stale = run(0.0);
    CHECK(stale.ghosts > 0);
    CHECK(!stale.atTrueFreq);
    Result settled = run(20.0);
    CHECK(settled.atTrueFreq);
    CHECK(settled.ghosts == 0);
}

// ------------------------------------------------------------------ view / UI
TEST(view_zoom_pan_reset_and_mapping) {
    FrequencyView v;
    v.setFullRange(100e6, 500e6, true);
    CHECK(!v.zoomed());
    v.zoom(4.0, 300e6);
    CHECK(v.zoomed());
    CHECK_NEAR(v.viewSpan(), 100e6, 1);
    // the anchor stays under the cursor
    CHECK_NEAR((300e6 - v.viewStart()) / v.viewSpan(), 0.5, 1e-9);
    v.pan(-1e9);
    CHECK_NEAR(v.viewStart(), 100e6, 1e-3);
    v.pan(1e9);
    CHECK_NEAR(v.viewStop(), 500e6, 1e-3);
    v.zoom(1e9, 400e6);
    CHECK_NEAR(v.viewSpan(), v.minSpanHz, 1e-6);
    v.zoom(1e-9, 400e6);
    CHECK(!v.zoomed());
    v.setView(200e6, 210e6);
    double x = v.freqToX(205e6, 10, 1000);
    CHECK_NEAR(x, 510, 1e-6);
    CHECK_NEAR(v.xToFreq(x, 10, 1000), 205e6, 1e-3);
    int b0, b1;
    v.reset();
    v.columnBins(0, 400, 4000, b0, b1);
    CHECK(b0 == 0 && b1 == 10);
    v.columnBins(399, 400, 4000, b0, b1);
    CHECK(b0 == 3990 && b1 == 4000);
    // Changing the swept range resets the view, the same range keeps it
    v.setView(200e6, 210e6);
    v.setFullRange(100e6, 500e6, false);
    CHECK(v.zoomed());
    v.setFullRange(88e6, 108e6, false);
    CHECK(!v.zoomed());
}

TEST(view_units_grid_autoscale_markers) {
    CHECK_NEAR(toUnit(433.92e6, FreqUnit::MHZ), 433.92, 1e-9);
    CHECK_NEAR(fromUnit(1.2, FreqUnit::GHZ), 1.2e9, 1e-3);
    CHECK_NEAR(fromUnit(500, FreqUnit::KHZ), 500e3, 1e-9);
    CHECK(std::string(unitName(FreqUnit::KHZ)) == "kHz");
    CHECK_NEAR(niceStep(20e6, 10), 2e6, 1e-6);
    CHECK_NEAR(niceStep(400e6, 8), 50e6, 1e-6);
    CHECK_NEAR(niceStep(1e3, 4), 200, 1e-9);
    float mn = 0, mx = 0;
    autoScaleRange(-97.3f, -31.0f, mn, mx);
    CHECK_NEAR(mn, -110.0, 1e-6);
    CHECK_NEAR(mx, -25.0, 1e-6);
    autoScaleRange(-90.0f, -88.0f, mn, mx);
    CHECK(mx - mn >= 30.0f);
    std::vector<Marker> m = { { "a", 100e6 }, { "b", 101e6 } };
    CHECK(nearestMarker(m, 100.9e6, 0.2e6) == 1);
    CHECK(nearestMarker(m, 100.5e6, 0.2e6) == -1);
    std::vector<float> tr = { -1, -2, -3, -4 };
    CHECK(traceValueAt(tr, 0, 4, 2.5) == -3);
    CHECK(!std::isfinite(traceValueAt(tr, 0, 4, 4.0)));
    CHECK(formatFrequency(433.92e6) == "433.9200 MHz");
    CHECK(formatFrequency(1.5e9, 3) == "1.500 GHz");
}

TEST(auto_parameters_follow_device) {
    SweepSettings s;
    EngineSettings e;
    e.displayBins = 4096;
    applyGenericAutoParams(s, e, 2.4e6, 20e6, 0.0);
    CHECK(e.fftSize == 512);
    CHECK_NEAR(s.usableFraction, 0.8, 1e-9);
    CHECK(s.overlapFraction >= minOverlapForDcCoverage(2.4e6 * 0.8, s.dcGuardHz));
    sweep::NativeSweepCaps caps;
    caps.sampleRateHz = 20e6;
    caps.samplesPerBlock = sweep::hackrf_format::BLOCK_SAMPLES;
    caps.maxDwellBlocks = 8;
    SweepSettings ns;
    EngineSettings ne;
    ne.displayBins = 16384;
    applyNativeAutoParams(ns, ne, caps, 40e6);
    CHECK(ne.fftSize == 4096); // limited by the 8184-sample HackRF block
    ns.autoParams = false;
    ne.fftSize = 512;
    ne.framesPerCapture = 100;
    applyNativeAutoParams(ns, ne, caps, 40e6);
    CHECK(ne.framesPerCapture == 15);
}

int main(int argc, char** argv) {
    std::string filter = (argc > 1) ? argv[1] : "";
    int passed = 0, failed = 0;
    for (auto& t : satest::registry()) {
        if (!filter.empty() && std::string(t.name).find(filter) == std::string::npos) { continue; }
        auto t0 = std::chrono::steady_clock::now();
        try {
            t.fn();
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            printf("[PASS] %-55s %8.1f ms\n", t.name, ms);
            passed++;
        }
        catch (const satest::Failure& f) {
            printf("[FAIL] %s\n       %s\n", t.name, f.msg.c_str());
            failed++;
        }
        catch (const std::exception& e) {
            printf("[FAIL] %s\n       exception: %s\n", t.name, e.what());
            failed++;
        }
    }
    printf("\n%d passed, %d failed, %d checks\n", passed, failed, satest::checks());
#ifdef SA_TEST_HAVE_FFTW
    printf("FFT backend: FFTW3f\n");
#else
    printf("FFT backend: reference radix-2 (FFTW not available)\n");
#endif
    printf("NOTE: synthetic tests only - hardware (HackRF) tests are NOT executed by this suite.\n");
    return failed == 0 ? 0 : 1;
}
