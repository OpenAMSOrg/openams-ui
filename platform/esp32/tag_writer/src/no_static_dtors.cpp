// The tag writer's C++ objects of static storage (the screen's state, the
// codec list, a few constants) register their destructors with __cxa_atexit()
// when they are constructed, so that exit() can run them. This firmware never
// exits - it restarts, and ESP-IDF's _exit() aborts - so those destructors
// never run, and registering them only kept the C library's table of exit
// handlers (384 bytes) in static RAM, where the display has none to spare.
// Defining __cxa_atexit() here, in the component that is its only user, keeps
// that table out of the image; nothing that runs behaves differently.

extern "C" int __cxa_atexit(void (*)(void *), void *, void *)
{
    return 0;
}
