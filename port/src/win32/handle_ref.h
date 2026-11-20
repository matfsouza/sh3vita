// Lifetime of a Win32 handle (tests/test_handle_ref.py). As on Windows, the object behind a handle stays alive while a
// call on that handle is running: each call holds a reference (h_acquire in handles.c), closing marks the handle
// closed, and whoever drops the last reference destroys the object; the handle number is only reused after that.
// The handle table's lock is held around every call.
#pragma once

typedef struct { int refs, closed; } HandleRef;

// A call starts on the handle; 0 once it was closed (the call fails as on a closed handle).
static inline int hr_acquire(HandleRef *r) {
    if (r->closed) return 0;
    r->refs++;
    return 1;
}
// The call ends; 1: the handle was closed meanwhile and this was the last call, destroy the object now.
static inline int hr_release(HandleRef *r) {
    return --r->refs == 0 && r->closed;
}
// Close (CloseHandle, mmioClose, FindClose...); 1: no call is running, destroy now (otherwise the last hr_release does).
static inline int hr_close(HandleRef *r) {
    if (r->closed) return 0;
    r->closed = 1;
    return r->refs == 0;
}
// The object still exists: open, or closed with a call still running (its handle number must not be reused yet).
static inline int hr_alive(const HandleRef *r) {
    return !r->closed || r->refs > 0;
}
