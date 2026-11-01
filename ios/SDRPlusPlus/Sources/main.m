// The whole app (UIKit delegate, OpenGL ES view, SDR++ core) lives in libsdrpp_core.dylib
extern int sdrppIosRun(int argc, char* argv[]);

int main(int argc, char* argv[]) {
    return sdrppIosRun(argc, argv);
}
