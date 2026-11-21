#include "screenshot.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef MITSHM
#include <sys/ipc.h>
#include <sys/shm.h>
#endif

Screenshot screenshot_new(Display *display, Window window)
{
    Screenshot result = {0};

    XWindowAttributes attributes;
    XGetWindowAttributes(display, window, &attributes);

#ifdef MITSHM
    result.shminfo = malloc(sizeof *result.shminfo);
    int screen = DefaultScreen(display);
    result.image = XShmCreateImage(display,
                                   DefaultVisual(display, screen),
                                   (unsigned int)DefaultDepth(display, screen),
                                   ZPixmap,
                                   NULL,
                                   result.shminfo,
                                   (unsigned int)attributes.width,
                                   (unsigned int)attributes.height);

    result.shminfo->shmid = shmget(IPC_PRIVATE,
                                   (size_t)result.image->bytes_per_line * (size_t)result.image->height,
                                   IPC_CREAT | 0777);
    result.shminfo->shmaddr = shmat(result.shminfo->shmid, NULL, 0);
    result.image->data = result.shminfo->shmaddr;
    result.shminfo->readOnly = False;

    XShmAttach(display, result.shminfo);
    XShmGetImage(display, window, result.image, 0, 0, AllPlanes);
#else
    result.image = XGetImage(display, window,
                             0, 0,
                             (unsigned int)attributes.width,
                             (unsigned int)attributes.height,
                             AllPlanes, ZPixmap);
#endif

    return result;
}

void screenshot_destroy(Screenshot *screenshot, Display *display)
{
#ifdef MITSHM
    XSync(display, False);
    XShmDetach(display, screenshot->shminfo);
    XDestroyImage(screenshot->image);
    shmdt(screenshot->shminfo->shmaddr);
    shmctl(screenshot->shminfo->shmid, IPC_RMID, NULL);
    free(screenshot->shminfo);
#else
    (void)display;
    XDestroyImage(screenshot->image);
#endif
}

void screenshot_refresh(Screenshot *screenshot, Display *display, Window window)
{
    XWindowAttributes attributes;
    XGetWindowAttributes(display, window, &attributes);

#ifdef MITSHM
    if (XShmGetImage(display, window, screenshot->image, 0, 0, AllPlanes) == 0 ||
        attributes.width != screenshot->image->width ||
        attributes.height != screenshot->image->height) {
        screenshot_destroy(screenshot, display);
        *screenshot = screenshot_new(display, window);
    }
#else
    XImage *refreshed = XGetSubImage(display, window,
                                     0, 0,
                                     (unsigned int)screenshot->image->width,
                                     (unsigned int)screenshot->image->height,
                                     AllPlanes, ZPixmap,
                                     screenshot->image,
                                     0, 0);
    if (refreshed == NULL ||
        refreshed->width != attributes.width ||
        refreshed->height != attributes.height) {
        XImage *new_image = XGetImage(display, window,
                                      0, 0,
                                      (unsigned int)attributes.width,
                                      (unsigned int)attributes.height,
                                      AllPlanes, ZPixmap);
        if (new_image != NULL) {
            XDestroyImage(screenshot->image);
            screenshot->image = new_image;
        }
    } else {
        screenshot->image = refreshed;
    }
#endif
}

void screenshot_save_to_ppm(const XImage *image, const char *file_path)
{
    FILE *f = fopen(file_path, "wb");
    if (!f) {
        perror(file_path);
        return;
    }

    fprintf(f, "P6\n%d %d\n255\n", image->width, image->height);
    for (int i = 0; i < image->width * image->height; ++i) {
        unsigned char px[3] = {
            (unsigned char)image->data[i * 4 + 2],
            (unsigned char)image->data[i * 4 + 1],
            (unsigned char)image->data[i * 4 + 0],
        };
        fwrite(px, 1, sizeof px, f);
    }

    fclose(f);
}
