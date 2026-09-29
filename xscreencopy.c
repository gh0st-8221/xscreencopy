#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
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

static void set_active_cursor(Display *dpy, Window win, Cursor cursor) {
    XChangeActivePointerGrab(dpy, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                             cursor, CurrentTime);
    XDefineCursor(dpy, win, cursor);
    XFlush(dpy);
}

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

    XSelectInput(dpy, win, ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyPressMask | KeyReleaseMask);

    Pixmap bg_pix = XCreatePixmap(dpy, win, screen_w, screen_h, DefaultDepth(dpy, DefaultScreen(dpy)));
    GC gc = XCreateGC(dpy, win, 0, NULL);
    XPutImage(dpy, bg_pix, gc, bg, 0, 0, 0, 0, screen_w, screen_h);
    XSetWindowBackgroundPixmap(dpy, win, bg_pix);
    XMapRaised(dpy, win);
    XSync(dpy, False);

    Cursor cross_cursor = XCreateFontCursor(dpy, XC_crosshair);
    Cursor pencil_cursor = XCreateFontCursor(dpy, XC_pencil);

    XGrabPointer(dpy, win, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                 GrabModeAsync, GrabModeAsync, win, cross_cursor, CurrentTime);

    for (int i = 0; i < 100; i++) {
        if (XGrabKeyboard(dpy, win, False, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess)
            break;
        usleep(1000);
    }

    XGCValues xor_vals;
    xor_vals.function = GXxor;
    xor_vals.foreground = WhitePixel(dpy, DefaultScreen(dpy));
    GC xor_gc = XCreateGC(dpy, win, GCFunction | GCForeground, &xor_vals);

    XColor blue_col;
    blue_col.red = 0;
    blue_col.green = 0;
    blue_col.blue = 65535;
    XAllocColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)), &blue_col);

    GC draw_gc = XCreateGC(dpy, win, 0, NULL);
    XSetForeground(dpy, draw_gc, blue_col.pixel);
    XSetLineAttributes(dpy, draw_gc, 3, LineSolid, CapRound, JoinRound);

    XEvent ev;
    int start_x = 0, start_y = 0, cur_x = 0, cur_y = 0;
    int prev_x = 0, prev_y = 0;
    int is_drawing_line = 0, is_selecting_rect = 0;
    int selecting = 1, canceled = 0, alt_pressed = 0;
    int drawn_x = 0, drawn_y = 0, drawn_w = 0, drawn_h = 0;

    while (selecting) {
        XNextEvent(dpy, &ev);
        if (ev.type == KeyPress) {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            if (ks == XK_Escape) {
                canceled = 1;
                selecting = 0;
                break;
            } else if (ks == XK_Alt_L || ks == XK_Alt_R || ks == XK_Meta_L || ks == XK_Meta_R) {
                if (!alt_pressed) {
                    alt_pressed = 1;
                    set_active_cursor(dpy, win, pencil_cursor);
                }
            }
        } else if (ev.type == KeyRelease) {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            if (ks == XK_Alt_L || ks == XK_Alt_R || ks == XK_Meta_L || ks == XK_Meta_R) {
                if (alt_pressed) {
                    alt_pressed = 0;
                    set_active_cursor(dpy, win, cross_cursor);
                }
            }
        } else if (ev.type == ButtonPress && ev.xbutton.button == Button1) {
            start_x = cur_x = prev_x = ev.xbutton.x;
            start_y = cur_y = prev_y = ev.xbutton.y;
            if (alt_pressed || (ev.xbutton.state & Mod1Mask)) {
                is_drawing_line = 1;
                set_active_cursor(dpy, win, pencil_cursor);
            } else {
                is_selecting_rect = 1;
                set_active_cursor(dpy, win, cross_cursor);
            }
        } else if (ev.type == MotionNotify) {
            cur_x = ev.xmotion.x;
            cur_y = ev.xmotion.y;
            if (is_drawing_line) {
                XDrawLine(dpy, win, draw_gc, prev_x, prev_y, cur_x, cur_y);
                XDrawLine(dpy, bg_pix, draw_gc, prev_x, prev_y, cur_x, cur_y);
                prev_x = cur_x;
                prev_y = cur_y;
                XFlush(dpy);
            } else if (is_selecting_rect) {
                if (drawn_w > 0 && drawn_h > 0)
                    XDrawRectangle(dpy, win, xor_gc, drawn_x, drawn_y, drawn_w, drawn_h);
                
                drawn_x = start_x < cur_x ? start_x : cur_x;
                drawn_y = start_y < cur_y ? start_y : cur_y;
                drawn_w = abs(cur_x - start_x);
                drawn_h = abs(cur_y - start_y);
                
                if (drawn_w > 0 && drawn_h > 0)
                    XDrawRectangle(dpy, win, xor_gc, drawn_x, drawn_y, drawn_w, drawn_h);
            }
        } else if (ev.type == ButtonRelease && ev.xbutton.button == Button1) {
            if (is_drawing_line) {
                is_drawing_line = 0;
                set_active_cursor(dpy, win, alt_pressed ? pencil_cursor : cross_cursor);
            } else if (is_selecting_rect) {
                is_selecting_rect = 0;
                selecting = 0;
            }
        }
    }

    int end_x = cur_x;
    int end_y = cur_y;
    if (start_x > end_x) { int t = start_x; start_x = end_x; end_x = t; }
    if (start_y > end_y) { int t = start_y; start_y = end_y; end_y = t; }
    int tw = end_x - start_x;
    int th = end_y - start_y;

    if (!canceled && tw > 0 && th > 0) {
        XImage *cropped = XGetImage(dpy, bg_pix, start_x, start_y, tw, th, AllPlanes, ZPixmap);
        if (cropped) {
            FILE *f = fopen("/tmp/xscreenshot.bmp", "wb");
            if (f) {
                int row_padded = (tw * 3 + 3) & (~3);
                BMPFileHeader fileHeader = {0x4D42, sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + row_padded * th, 0, 0, sizeof(BMPFileHeader) + sizeof(BMPInfoHeader)};
                BMPInfoHeader infoHeader = {sizeof(BMPInfoHeader), tw, th, 1, 24, 0, row_padded * th, 2835, 2835, 0, 0};

                fwrite(&fileHeader, sizeof(fileHeader), 1, f);
                fwrite(&infoHeader, sizeof(infoHeader), 1, f);

                unsigned char *row = malloc(row_padded);
                for (int y = th - 1; y >= 0; y--) {
                    for (int x = 0; x < tw; x++) {
                        uint32_t p = XGetPixel(cropped, x, y);
                        row[x * 3 + 0] = p & 0xff;
                        row[x * 3 + 1] = (p >> 8) & 0xff;
                        row[x * 3 + 2] = (p >> 16) & 0xff;
                    }
                    for (int x = tw * 3; x < row_padded; x++) {
                        row[x] = 0;
                    }
                    fwrite(row, row_padded, 1, f);
                }
                free(row);
                fclose(f);

                system("convert /tmp/xscreenshot.bmp /tmp/xscreenshot.png 2>/dev/null || "
                       "ffmpeg -y -i /tmp/xscreenshot.bmp /tmp/xscreenshot.png 2>/dev/null");

                if (access("/tmp/xscreenshot.png", F_OK) == 0) {
                    system("xclip -selection clipboard -t image/png -i /tmp/xscreenshot.png 2>/dev/null || "
                           "wl-copy -t image/png < /tmp/xscreenshot.png 2>/dev/null");
                } else {
                    system("xclip -selection clipboard -t image/bmp -i /tmp/xscreenshot.bmp 2>/dev/null || "
                           "xclip -selection clipboard -t image/x-bmp -i /tmp/xscreenshot.bmp 2>/dev/null || "
                           "wl-copy < /tmp/xscreenshot.bmp 2>/dev/null");
                }
            }
            XDestroyImage(cropped);
        }
    }

    XUngrabPointer(dpy, CurrentTime);
    XUngrabKeyboard(dpy, CurrentTime);
    XDestroyWindow(dpy, win);
    XFreePixmap(dpy, bg_pix);
    XFreeGC(dpy, gc);
    XFreeGC(dpy, xor_gc);
    XFreeGC(dpy, draw_gc);
    XFreeCursor(dpy, cross_cursor);
    XFreeCursor(dpy, pencil_cursor);
    XDestroyImage(bg);
    XCloseDisplay(dpy);
    return 0;
}
