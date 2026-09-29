#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>

#pragma pack(push, 1)
typedef struct {
    uint16_t fileType;
    uint32_t fileSize;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t dataOffset;
} BMPFileHeader;

typedef struct {
    uint32_t headerSize;
    int32_t  width;
    int32_t  height;
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
    uint32_t imageSize;
    int32_t  xPelsPerMeter;
    int32_t  yPelsPerMeter;
    uint32_t clrUsed;
    uint32_t clrImportant;
} BMPInfoHeader;
#pragma pack(pop)

int main(void) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return 1;

    Window root = DefaultRootWindow(dpy);
    int screen_w = DisplayWidth(dpy, DefaultScreen(dpy));
    int screen_h = DisplayHeight(dpy, DefaultScreen(dpy));

    XImage *bg = XGetImage(dpy, root, 0, 0, screen_w, screen_h, AllPlanes, ZPixmap);
    if (!bg) {
        XCloseDisplay(dpy);
        return 1;
    }

    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.background_pixel = BlackPixel(dpy, DefaultScreen(dpy));
    Window win = XCreateWindow(dpy, root, 0, 0, screen_w, screen_h, 0,
                               CopyFromParent, InputOutput, CopyFromParent,
                               CWOverrideRedirect | CWBackPixel, &attrs);

    Pixmap bg_pix = XCreatePixmap(dpy, win, screen_w, screen_h, DefaultDepth(dpy, DefaultScreen(dpy)));
    GC gc = XCreateGC(dpy, win, 0, NULL);
    XPutImage(dpy, bg_pix, gc, bg, 0, 0, 0, 0, screen_w, screen_h);
    XSetWindowBackgroundPixmap(dpy, win, bg_pix);
    XMapRaised(dpy, win);

    Cursor cross = XCreateFontCursor(dpy, XC_crosshair);
    XGrabPointer(dpy, win, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                 GrabModeAsync, GrabModeAsync, win, cross, CurrentTime);
    XGrabKeyboard(dpy, win, False, GrabModeAsync, GrabModeAsync, CurrentTime);

    XGCValues xor_vals;
    xor_vals.function = GXxor;
    xor_vals.foreground = WhitePixel(dpy, DefaultScreen(dpy));
    GC xor_gc = XCreateGC(dpy, win, GCFunction | GCForeground, &xor_vals);

    XEvent ev;
    int start_x = 0, start_y = 0, cur_x = 0, cur_y = 0;
    int drawing = 0, selecting = 1;
    int drawn_x = 0, drawn_y = 0, drawn_w = 0, drawn_h = 0;

    while (selecting && !XNextEvent(dpy, &ev)) {
        if (ev.type == ButtonPress && ev.xbutton.button == Button1) {
            start_x = cur_x = ev.xbutton.x;
            start_y = cur_y = ev.xbutton.y;
            drawing = 1;
        } else if (ev.type == MotionNotify && drawing) {
            if (drawn_w > 0 && drawn_h > 0)
                XDrawRectangle(dpy, win, xor_gc, drawn_x, drawn_y, drawn_w, drawn_h);
            
            cur_x = ev.xmotion.x;
            cur_y = ev.xmotion.y;
            
            drawn_x = start_x < cur_x ? start_x : cur_x;
            drawn_y = start_y < cur_y ? start_y : cur_y;
            drawn_w = abs(cur_x - start_x);
            drawn_h = abs(cur_y - start_y);
            
            if (drawn_w > 0 && drawn_h > 0)
                XDrawRectangle(dpy, win, xor_gc, drawn_x, drawn_y, drawn_w, drawn_h);

        } else if (ev.type == ButtonRelease && ev.xbutton.button == Button1) {
            selecting = 0;
        } else if (ev.type == KeyPress) {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            if (ks == XK_Escape) {
                XDestroyImage(bg);
                XUngrabPointer(dpy, CurrentTime);
                XUngrabKeyboard(dpy, CurrentTime);
                XDestroyWindow(dpy, win);
                XFreePixmap(dpy, bg_pix);
                XFreeGC(dpy, gc);
                XFreeGC(dpy, xor_gc);
                XFreeCursor(dpy, cross);
                XCloseDisplay(dpy);
                return 0;
            }
        }
    }

    XUngrabPointer(dpy, CurrentTime);
    XUngrabKeyboard(dpy, CurrentTime);
    XDestroyWindow(dpy, win);
    XFreePixmap(dpy, bg_pix);
    XFreeGC(dpy, gc);
    XFreeGC(dpy, xor_gc);
    XFreeCursor(dpy, cross);
    XSync(dpy, False);

    int end_x = cur_x;
    int end_y = cur_y;
    if (start_x > end_x) { int t = start_x; start_x = end_x; end_x = t; }
    if (start_y > end_y) { int t = start_y; start_y = end_y; end_y = t; }
    int tw = end_x - start_x;
    int th = end_y - start_y;

    if (tw > 0 && th > 0) {
        uint32_t *crop_data = malloc(tw * th * sizeof(uint32_t));
        for (int y = 0; y < th; y++) {
            for (int x = 0; x < tw; x++) {
                crop_data[y * tw + x] = XGetPixel(bg, start_x + x, start_y + y);
            }
        }

        FILE *pipe = popen("convert - png:- 2>/dev/null | xclip -selection clipboard -t image/png 2>/dev/null || xclip -selection clipboard -t image/bmp", "w");
        if (pipe) {
            int row_padded = (tw * 3 + 3) & (~3);
            BMPFileHeader fileHeader = {0x4D42, sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + row_padded * th, 0, 0, sizeof(BMPFileHeader) + sizeof(BMPInfoHeader)};
            BMPInfoHeader infoHeader = {sizeof(BMPInfoHeader), tw, th, 1, 24, 0, row_padded * th, 2835, 2835, 0, 0};

            fwrite(&fileHeader, sizeof(fileHeader), 1, pipe);
            fwrite(&infoHeader, sizeof(infoHeader), 1, pipe);

            unsigned char *row = malloc(row_padded);
            for (int y = th - 1; y >= 0; y--) {
                for (int x = 0; x < tw; x++) {
                    uint32_t p = crop_data[y * tw + x];
                    row[x * 3 + 0] = p & 0xff;
                    row[x * 3 + 1] = (p >> 8) & 0xff;
                    row[x * 3 + 2] = (p >> 16) & 0xff;
                }
                for (int x = tw * 3; x < row_padded; x++) {
                    row[x] = 0;
                }
                fwrite(row, row_padded, 1, pipe);
            }
            free(row);
            pclose(pipe);
        }
        free(crop_data);
    }

    XDestroyImage(bg);
    XCloseDisplay(dpy);
    return 0;
}
