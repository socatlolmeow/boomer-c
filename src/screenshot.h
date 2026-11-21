#ifndef BOOMER_SCREENSHOT_H
#define BOOMER_SCREENSHOT_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#ifdef MITSHM
#include <X11/extensions/XShm.h>
#endif

typedef struct {
    XImage *image;
#ifdef MITSHM
    XShmSegmentInfo *shminfo;
#endif
} Screenshot;

Screenshot screenshot_new(Display *display, Window window);
void screenshot_destroy(Screenshot *screenshot, Display *display);
void screenshot_refresh(Screenshot *screenshot, Display *display, Window window);
void screenshot_save_to_ppm(const XImage *image, const char *file_path);

#endif
