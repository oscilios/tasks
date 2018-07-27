#include "tasks/ThreadUtilities.h"

// clang-format off
#include <xmmintrin.h>
#include <cfenv>
#include <pthread.h>
#include <signal.h>
#include <sys/signal.h>

// flush-to-zero denormals
#ifndef _MM_DENORMALS_ZERO_MASK
    #define _MM_DENORMALS_ZERO_MASK 0x0040
    #define _MM_DENORMALS_ZERO_ON 0x0040
    #define _MM_DENORMALS_ZERO_OFF 0x0000
    #define _MM_SET_DENORMALS_ZERO_MODE(mode) \
        _mm_setcsr((_mm_getcsr() & ~_MM_DENORMALS_ZERO_MASK) | (mode))
    #define _MM_GET_DENORMALS_ZERO_MODE() (_mm_getcsr() & _MM_DENORMALS_ZERO_MASK)
#endif

#if __APPLE__
    #include <mach/mach_init.h>
    #include <mach/mach_error.h>
    #include <mach/thread_policy.h>
    #include <mach/thread_act.h>
    #include <CoreAudio/HostTime.h>
#endif
// clang-format on

void tasks::flushDenormalsToZero()
{
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
}
void tasks::setRealtimePriority(std::thread* thread)
{
#if __APPLE__
    // REAL-TIME / TIME-CONSTRAINT THREAD
    pthread_t inThread                = thread ? thread->native_handle() : pthread_self();
    UInt64 cycleDurationInNanoseconds = 10000000;
    thread_time_constraint_policy_data_t theTCPolicy;
    UInt64 theComputeQuanta;
    UInt64 thePeriod;
    UInt64 thePeriodNanos;

    thePeriodNanos   = cycleDurationInNanoseconds;
    theComputeQuanta = AudioConvertNanosToHostTime(thePeriodNanos * 0.15);
    thePeriod        = AudioConvertNanosToHostTime(thePeriodNanos);

    theTCPolicy.period      = thePeriod;
    theTCPolicy.computation = theComputeQuanta;
    theTCPolicy.constraint  = thePeriod;
    theTCPolicy.preemptible = true;
    thread_policy_set(pthread_mach_thread_np(inThread),
                      THREAD_TIME_CONSTRAINT_POLICY,
                      (thread_policy_t)&theTCPolicy,
                      THREAD_TIME_CONSTRAINT_POLICY_COUNT);
#endif
}
