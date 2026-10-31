// *****************************************************************************
    // start include guard
    #ifndef TRACING_HPP
    #define TRACING_HPP
// *****************************************************************************


// =============================================================================
//      PROFILING TRACE CONTROL
// =============================================================================
// The recorder itself is V32Trace (ConsoleLogic). These functions are the
// user side of it: the menu and the keys only post a request, and the
// request is served once per update, outside GUI processing, because
// writing files from inside GUI code is not safe under Linux (see GUI.cpp)


void Trace_RequestToggle();     // start / stop recording (F7)
void Trace_RequestMarker();     // mark the current moment (F8)
void Trace_ProcessRequests();   // call once per update, before running frames


// *****************************************************************************
    // end include guard
    #endif
// *****************************************************************************
