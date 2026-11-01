#pragma once

#if defined(_WIN32)
#include <windows.h>
#include <GL/gl.h>
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IOS
#include <OpenGLES/ES3/gl.h>
#else
#include <OpenGL/gl.h>
#endif
#elif defined(__ANDROID__)
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#else
#include <GL/gl.h>
#endif