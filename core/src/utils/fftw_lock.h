#pragma once
#include <mutex>
#include <sdrpp_export.h>

// The FFTW planner (plan creation and destruction) is not thread safe, only executing a plan is.
// Everything that plans or destroys a plan must hold this lock.
SDRPP_EXPORT std::mutex& fftwPlannerMutex();
