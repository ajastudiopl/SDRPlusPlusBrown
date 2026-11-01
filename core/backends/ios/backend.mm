#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <OpenGLES/EAGL.h>
#import <OpenGLES/EAGLDrawable.h>
#import <AVFoundation/AVFoundation.h>
#include <backend.h>
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "implot/implot.h"
#include <utils/flog.h>
#include <utils/opengl_include_code.h>
#include <core.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <gui/menus/display.h>
#include <utils/usleep.h>
#include <http_debug_server.h>
#include <algorithm>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <ctm.h>

// iOS backend: UIKit window + OpenGL ES 3 view, touch input mapped to the ImGui mouse.

static uint64_t glContextGen = 0;

@interface SDRGLView : UIView <UIKeyInput> {
    UITouch* touchSlots[10];
}
@end

static SDRGLView* gView = nil;
static EAGLContext* gContext = nil;
static GLuint gFbo = 0, gRbo = 0;
static int gFbWidth = 0, gFbHeight = 0;
static std::atomic<bool> gNeedResize{ true };
static std::atomic<bool> gQuit{ false };
static std::atomic<bool> gBackground{ false };

// UIKit runs on the main thread, SDR++ on its own thread: input is queued here
static std::mutex gInputMtx;
static std::vector<std::function<void(ImGuiIO&)>> gInputQueue;
static void queueInput(std::function<void(ImGuiIO&)> fn) {
    std::lock_guard<std::mutex> lck(gInputMtx);
    gInputQueue.push_back(std::move(fn));
}
static int gArgc = 0;
static char** gArgv = nullptr;

@implementation SDRGLView
+ (Class)layerClass { return [CAEAGLLayer class]; }

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.multipleTouchEnabled = YES;
        self.contentScaleFactor = [UIScreen mainScreen].scale;
        CAEAGLLayer* layer = (CAEAGLLayer*)self.layer;
        layer.opaque = YES;
        layer.drawableProperties = @{ kEAGLDrawablePropertyRetainedBacking : @NO,
                                      kEAGLDrawablePropertyColorFormat : kEAGLColorFormatRGBA8 };
    }
    return self;
}

- (void)layoutSubviews {
    [super layoutSubviews];
    gNeedResize = true;
}

// On-screen keyboard: shown while ImGui wants text input
- (BOOL)canBecomeFirstResponder { return YES; }
- (BOOL)hasText { return YES; }
- (UITextAutocorrectionType)autocorrectionType { return UITextAutocorrectionTypeNo; }
- (UITextAutocapitalizationType)autocapitalizationType { return UITextAutocapitalizationTypeNone; }
- (UITextSpellCheckingType)spellCheckingType { return UITextSpellCheckingTypeNo; }

- (void)pressKey:(ImGuiKey)key {
    queueInput([key](ImGuiIO& io) {
        io.AddKeyEvent(key, true);
        io.AddKeyEvent(key, false);
    });
}

- (void)insertText:(NSString*)text {
    if ([text isEqualToString:@"\n"]) { [self pressKey:ImGuiKey_Enter]; return; }
    std::string str([text UTF8String]);
    queueInput([str](ImGuiIO& io) { io.AddInputCharactersUTF8(str.c_str()); });
}

- (void)deleteBackward {
    [self pressKey:ImGuiKey_Backspace];
}

// Multi-touch: every finger gets a slot (ImGui finger API drives scrolling and pinch);
// a tap becomes a click, a horizontal drag becomes a mouse drag, like on Android.
- (int)slotForTouch:(UITouch*)t create:(BOOL)create {
    for (int i = 0; i < 10; i++) { if (touchSlots[i] == t) { return i; } }
    if (!create) { return -1; }
    for (int i = 0; i < 10; i++) {
        if (touchSlots[i] == nil) { touchSlots[i] = t; return i; }
    }
    return -1;
}

- (void)handleTouches:(NSSet<UITouch*>*)touches phase:(int)phase {
    float scale = self.contentScaleFactor;
    for (UITouch* t in touches) {
        int slot = [self slotForTouch:t create:(phase == 0)];
        if (slot < 0) { continue; }
        CGPoint p = [t locationInView:self];
        float x = p.x * scale, y = p.y * scale;
        if (phase == 2) { touchSlots[slot] = nil; }
        queueInput([slot, phase, x, y](ImGuiIO& io) {
            static int primary = -1;
            static float startX = 0, startY = 0;
            static bool mouseDrag = false;
            const float threshold = 12.0f;
            if (phase == 0) {
                io.AddFingerEvent(slot, true);
                io.AddFingerMotion(slot, x, y);
                io.AddMousePosEvent(x, y);
                if (primary == -1) {
                    primary = slot;
                    startX = x;
                    startY = y;
                    mouseDrag = false;
                }
            }
            else if (phase == 1) {
                io.AddFingerMotion(slot, x, y);
                if (slot == primary) {
                    float dx = x - startX, dy = y - startY;
                    bool horizontalDrag = fabsf(dx) >= threshold && fabsf(dx) > fabsf(dy) * 1.2f;
                    if (!mouseDrag && horizontalDrag) {
                        mouseDrag = true;
                        io.AddMousePosEvent(startX, startY);
                        io.AddMouseButtonEvent(0, true);
                    }
                }
                io.AddMousePosEvent(x, y);
            }
            else {
                io.AddFingerMotion(slot, x, y);
                io.AddFingerEvent(slot, false);
                if (slot == primary) {
                    if (mouseDrag) {
                        io.AddMousePosEvent(x, y);
                        io.AddMouseButtonEvent(0, false);
                    }
                    else {
                        float dx = x - startX, dy = y - startY;
                        if (dx * dx + dy * dy < threshold * threshold) {
                            io.AddMousePosEvent(x, y);
                            io.AddMouseButtonEvent(0, true);
                            io.AddMouseButtonEvent(0, false);
                        }
                    }
                    primary = -1;
                    mouseDrag = false;
                }
            }
        });
    }
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self handleTouches:touches phase:0]; }
- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self handleTouches:touches phase:1]; }
- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self handleTouches:touches phase:2]; }
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self handleTouches:touches phase:2]; }
@end

@interface SDRViewController : UIViewController
@end
@implementation SDRViewController
- (void)loadView { self.view = [[SDRGLView alloc] initWithFrame:[UIScreen mainScreen].bounds]; }
- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskAll; }
@end

@interface SDRAppDelegate : UIResponder <UIApplicationDelegate>
@property(strong, nonatomic) UIWindow* window;
@end

@implementation SDRAppDelegate
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    SDRViewController* vc = [[SDRViewController alloc] init];
    self.window.rootViewController = vc;
    [self.window makeKeyAndVisible];
    gView = (SDRGLView*)vc.view;
    [UIApplication sharedApplication].idleTimerDisabled = YES;

    NSString* bundle = [[NSBundle mainBundle] bundlePath];
    setenv("SDRPP_MODULES_DIR", [[bundle stringByAppendingPathComponent:@"Frameworks"] UTF8String], 1);
    setenv("SDRPP_RES_DIR", [[bundle stringByAppendingPathComponent:@"res"] UTF8String], 1);

    // SDR++ runs on its own thread so the main thread stays free for UIKit (keyboard, gestures)
    NSThread* thread = [[NSThread alloc] initWithBlock:^{
        sdrpp_main(gArgc, gArgv);
        exit(0);
    }];
    thread.stackSize = 16 * 1024 * 1024;
    thread.qualityOfService = NSQualityOfServiceUserInteractive;
    [thread start];
    return YES;
}
- (void)applicationDidEnterBackground:(UIApplication*)application { gBackground = true; }
- (void)applicationWillEnterForeground:(UIApplication*)application { gBackground = false; }
@end

extern "C" void sdrppIosActivateAudioSession() {
    NSError* err = nil;
    AVAudioSession* session = [AVAudioSession sharedInstance];
    [session setCategory:AVAudioSessionCategoryPlayback error:&err];
    [session setActive:YES error:&err];
    if (err) { flog::error("AVAudioSession: {}", std::string([[err localizedDescription] UTF8String])); }
}

extern "C" SDRPP_EXPORT int sdrppIosRun(int argc, char* argv[]) {
    gArgc = argc;
    gArgv = argv;
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([SDRAppDelegate class]));
    }
}

// Plain GET used by the markers backup; "#ERROR:..." in the answer means failure (same convention as on Android)
std::string iosHttpGet(const std::string& url) {
    NSURL* nsurl = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
    if (!nsurl) { return "#ERROR:invalid address"; }
    __block std::string result = "#ERROR:no answer";
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:nsurl cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:20];
    [[[NSURLSession sharedSession] dataTaskWithRequest:req completionHandler:^(NSData* data, NSURLResponse* resp, NSError* err) {
        if (err) { result = std::string("#ERROR:") + [[err localizedDescription] UTF8String]; }
        else if ([resp isKindOfClass:[NSHTTPURLResponse class]] && [(NSHTTPURLResponse*)resp statusCode] >= 400) {
            result = "#ERROR:HTTP " + std::to_string((int)[(NSHTTPURLResponse*)resp statusCode]);
        }
        else if (data) { result = std::string((const char*)[data bytes], [data length]); }
        dispatch_semaphore_signal(done);
    }] resume];
    dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 25 * NSEC_PER_SEC));
    return result;
}

static const char* iosGetClipboard(void*) {
    static std::string text;
    NSString* s = [UIPasteboard generalPasteboard].string;
    text = s ? [s UTF8String] : "";
    return text.c_str();
}

static void iosSetClipboard(void*, const char* text) {
    [UIPasteboard generalPasteboard].string = [NSString stringWithUTF8String:text];
}

namespace backend {
    static void createFramebuffer() {
        if (gFbo) { glDeleteFramebuffers(1, &gFbo); }
        if (gRbo) { glDeleteRenderbuffers(1, &gRbo); }
        glGenFramebuffers(1, &gFbo);
        glGenRenderbuffers(1, &gRbo);
        glBindFramebuffer(GL_FRAMEBUFFER, gFbo);
        glBindRenderbuffer(GL_RENDERBUFFER, gRbo);
        [gContext renderbufferStorage:GL_RENDERBUFFER fromDrawable:(CAEAGLLayer*)gView.layer];
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, gRbo);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &gFbWidth);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &gFbHeight);
        gNeedResize = false;
    }

    int init(std::string resDir) {
        // Wait for the UIKit view to exist (created in didFinishLaunching)
        while (gView == nil) {
            usleep(10000);
        }

        gContext = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
        if (gContext == nil || ![EAGLContext setCurrentContext:gContext]) {
            flog::error("Could not create an OpenGL ES 3 context");
            return -1;
        }
        createFramebuffer();

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = NULL;
        io.GetClipboardTextFn = iosGetClipboard;
        io.SetClipboardTextFn = iosSetClipboard;

        // Scale the UI to the screen density on first start
        core::configManager.acquire();
        float screenScale = (float)gView.contentScaleFactor;
        if ((float)core::configManager.conf["uiScale"] == 1.0f) {
            core::configManager.conf["uiScale"] = screenScale;
            style::uiScale = screenScale;
        }
        core::configManager.release(true);

        if (!ImGui_ImplOpenGL3_Init("#version 300 es")) { return -1; }
        glContextGen++;
        return 0;
    }

    uint64_t glContextGeneration() {
        return glContextGen;
    }

    void beginFrame() {
        static CFAbsoluteTime last = CFAbsoluteTimeGetCurrent();
        CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)gFbWidth, (float)gFbHeight);
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        io.DeltaTime = std::max(1.0f / 1000.0f, (float)(now - last));
        last = now;

        std::vector<std::function<void(ImGuiIO&)>> pending;
        {
            std::lock_guard<std::mutex> lck(gInputMtx);
            pending.swap(gInputQueue);
        }
        for (auto& fn : pending) { fn(io); }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
    }

    void render(bool vsync) {
        ImGui::Render();
        glBindFramebuffer(GL_FRAMEBUFFER, gFbo);
        glViewport(0, 0, gFbWidth, gFbHeight);
        glClearColor(gui::themeManager.clearColor.x, gui::themeManager.clearColor.y, gui::themeManager.clearColor.z, gui::themeManager.clearColor.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glBindRenderbuffer(GL_RENDERBUFFER, gRbo);
        [gContext presentRenderbuffer:GL_RENDERBUFFER];
    }

    void getMouseScreenPos(double& x, double& y) { x = 0; y = 0; }
    void setMouseScreenPos(double x, double y) {}

    int renderLoop() {
        while (!gQuit) {
            // One frame period
            usleep(gBackground ? 200000 : 16000);
            if (gBackground) { continue; }
            if (gNeedResize) { createFramebuffer(); }
            if (gFbWidth <= 0 || gFbHeight <= 0) { continue; }

            auto ctm = currentTimeNanos() / 1000;
            beginFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2((float)gFbWidth, (float)gFbHeight));
            gui::mainWindow.draw();

            // Keep the keyboard up for a moment after ImGui stops asking, to avoid flicker
            static bool keyboardShown = false;
            static int noKeyboardFrames = 0;
            bool wantKeyboard = ImGui::GetIO().WantTextInput;
            noKeyboardFrames = wantKeyboard ? 0 : noKeyboardFrames + 1;
            if (wantKeyboard && !keyboardShown) {
                keyboardShown = true;
                dispatch_async(dispatch_get_main_queue(), ^{ [gView becomeFirstResponder]; });
            }
            else if (!wantKeyboard && keyboardShown && noKeyboardFrames > 30) {
                keyboardShown = false;
                dispatch_async(dispatch_get_main_queue(), ^{ [gView resignFirstResponder]; });
            }

            if (httpdebug::getSdrStartRequest()) {
                gui::mainWindow.setPlayState(true);
                httpdebug::setSdrPlaying(true);
            }
            if (httpdebug::getSdrStopRequest()) {
                gui::mainWindow.setPlayState(false);
                httpdebug::setSdrPlaying(false);
            }
            httpdebug::procfs::processQueue();

            render();
            httpdebug::clearWidgetRegistry();
            ctm = currentTimeNanos() / 1000 - ctm;
            lastDrawTimeBackend = ctm - lastDrawTime;
        }
        return 0;
    }

    int end() {
        ImGui_ImplOpenGL3_Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
        return 0;
    }
}
