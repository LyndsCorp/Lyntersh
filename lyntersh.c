/*
 * Lyntersh - Zenity moderno con Xlib, Xft (UTF-8), temas, icono PNG y colores
 * Compilar: gcc -o lyntersh lyntersh.c $(pkg-config --cflags --libs xft fontconfig freetype2) -lX11 -lpng -lm -O2
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <fontconfig/fontconfig.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <png.h>

#define BUF_SIZE 1024
#define MAX_LINES 500

typedef enum {
    DIALOG_ENTRY, DIALOG_ERROR, DIALOG_INFO,
    DIALOG_QUESTION, DIALOG_WARNING, DIALOG_TEXT_INFO
} DialogType;

typedef enum { THEME_LIGHT, THEME_DARK } Theme;

typedef struct {
    int x, y, w, h;
    int hover, pressed;
    const char *label;
} Button;

typedef struct {
    Display *dpy;
    Window win;
    Visual *vis;
    Colormap cmap;
    int screen;
    int depth;
    unsigned long black, white;
    int width, height;
    DialogType type;
    char *title, *text, *filename;
    char *icon_path;
    char *wm_class;
    unsigned long btn_normal, btn_hover, btn_press;
    Theme theme;
    XftColor clr_bg, clr_text, clr_entry_bg, clr_entry_border;
    XftColor clr_scroll_bg, clr_scroll_thumb;
    char entry_buf[BUF_SIZE];
    int entry_cursor, entry_len;
    int done, result;
    Button btn_ok, btn_cancel, btn_yes, btn_no;
    int entry_x, entry_y, entry_w, entry_h;
    char **lines;
    int nlines, scroll_y;
    int scrollbar_x, scrollbar_w;
    double scrollbar_h, scrollbar_y;
    Cursor cur_hand, cur_xterm, current_cursor;
    Pixmap icon_pixmap, icon_mask;
    XftFont *xft_font;
    XftDraw *xft_draw;
    GC gc;
} Dialog;

/* ---------- Asignar color Xft ---------- */
static void set_xft_color(XftColor *c, unsigned long rgb) {
    c->pixel = rgb;
    c->color.red   = ((rgb >> 16) & 0xFF) * 0x101;
    c->color.green = ((rgb >> 8) & 0xFF) * 0x101;
    c->color.blue  = (rgb & 0xFF) * 0x101;
    c->color.alpha = 0xFFFF;
}

/* ---------- Aplicar tema ---------- */
static void apply_theme(Dialog *d) {
    unsigned long bg, text, entry_bg, entry_border, scroll_bg, scroll_thumb;
    if (d->theme == THEME_LIGHT) {
        bg = 0xE8E8E8; text = 0x000000; entry_bg = 0xFFFFFF;
        entry_border = 0x888888; scroll_bg = 0xCCCCCC; scroll_thumb = 0xB0B0B0;
    } else {
        bg = 0x2E2E2E; text = 0xFFFFFF; entry_bg = 0x3C3C3C;
        entry_border = 0x555555; scroll_bg = 0x555555; scroll_thumb = 0x888888;
    }
    set_xft_color(&d->clr_bg, bg);
    set_xft_color(&d->clr_text, text);
    set_xft_color(&d->clr_entry_bg, entry_bg);
    set_xft_color(&d->clr_entry_border, entry_border);
    set_xft_color(&d->clr_scroll_bg, scroll_bg);
    set_xft_color(&d->clr_scroll_thumb, scroll_thumb);
}

/* ---------- Cargar fuente con Xft ---------- */
static XftFont* load_xft_font(Display *dpy, int screen) {
    FcPattern *pat = FcNameParse((const FcChar8*)"sans-serif-12");
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult result;
    FcPattern *match = FcFontMatch(NULL, pat, &result);
    XftFont *font = XftFontOpenPattern(dpy, match);
    FcPatternDestroy(pat);
    return font;
}

/* ---------- Utilidades de color ---------- */
static unsigned long adjust_brightness(unsigned long rgb, double factor) {
    int r = (rgb >> 16) & 0xFF; int g = (rgb >> 8) & 0xFF; int b = rgb & 0xFF;
    r = (int)(r * factor); if (r > 255) r = 255; if (r < 0) r = 0;
    g = (int)(g * factor); if (g > 255) g = 255; if (g < 0) g = 0;
    b = (int)(b * factor); if (b > 255) b = 255; if (b < 0) b = 0;
    return (r << 16) | (g << 8) | b;
}

static int parse_hex_color(const char *hex, unsigned long *rgb) {
    if (strlen(hex) != 7 || hex[0] != '#') return 0;
    char *end;
    unsigned long val = strtoul(hex+1, &end, 16);
    if (*end != '\0') return 0;
    *rgb = val;
    return 1;
}

static void set_button_colors(Dialog *d, const char *spec) {
    if (strcmp(spec, "green") == 0) {
        d->btn_normal = 0x4CAF50; d->btn_hover = 0x66BB6A; d->btn_press = 0x388E3C;
    } else if (strcmp(spec, "red") == 0) {
        d->btn_normal = 0xF44336; d->btn_hover = 0xEF5350; d->btn_press = 0xD32F2F;
    } else if (strcmp(spec, "blue") == 0) {
        d->btn_normal = 0x2196F3; d->btn_hover = 0x42A5F5; d->btn_press = 0x1976D2;
    } else if (strcmp(spec, "orange") == 0) {
        d->btn_normal = 0xFF9800; d->btn_hover = 0xFFA726; d->btn_press = 0xF57C00;
    } else if (strcmp(spec, "purple") == 0) {
        d->btn_normal = 0x9C27B0; d->btn_hover = 0xAB47BC; d->btn_press = 0x7B1FA2;
    } else if (spec[0] == '#') {
        unsigned long rgb;
        if (parse_hex_color(spec, &rgb)) {
            d->btn_normal = rgb;
            d->btn_hover  = adjust_brightness(rgb, 1.2);
            d->btn_press  = adjust_brightness(rgb, 0.8);
        }
    }
}

/* ---------- Icono PNG ---------- */
static unsigned char* load_png_rgba(const char *filename, int *w, int *h) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return NULL;
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) { fclose(fp); return NULL; }
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, NULL, NULL); fclose(fp); return NULL; }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL); fclose(fp); return NULL;
    }
    png_init_io(png, fp);
    png_read_info(png, info);
    *w = png_get_image_width(png, info);
    *h = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth  = png_get_bit_depth(png, info);
    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    png_read_update_info(png, info);
    int rowbytes = png_get_rowbytes(png, info);
    unsigned char *data = malloc(rowbytes * (*h));
    png_bytep *row_pointers = malloc(sizeof(png_bytep) * (*h));
    for (int y = 0; y < *h; y++) row_pointers[y] = data + y * rowbytes;
    png_read_image(png, row_pointers);
    png_destroy_read_struct(&png, &info, NULL);
    free(row_pointers);
    fclose(fp);
    return data;
}

static void load_icon(Dialog *d, const char *path) {
    int w, h;
    unsigned char *rgba = load_png_rgba(path, &w, &h);
    if (!rgba) { fprintf(stderr, "No se pudo cargar el icono PNG '%s'\n", path); return; }
    XImage *ximg = XCreateImage(d->dpy, d->vis, d->depth, ZPixmap, 0, NULL, w, h, 32, 0);
    if (!ximg) { free(rgba); return; }
    ximg->data = malloc(ximg->bytes_per_line * h);
    unsigned long *icon_data = malloc((2 + w * h) * sizeof(unsigned long));
    icon_data[0] = w; icon_data[1] = h;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            unsigned char *p = rgba + (y*w + x)*4;
            unsigned long pixel = (p[0]<<16)|(p[1]<<8)|p[2];
            XPutPixel(ximg, x, y, pixel);
            icon_data[2 + y*w + x] = (p[3]<<24)|(p[0]<<16)|(p[1]<<8)|p[2];
        }
    }
    free(rgba);
    GC gc = XCreateGC(d->dpy, d->win, 0, NULL);
    d->icon_pixmap = XCreatePixmap(d->dpy, d->win, w, h, d->depth);
    XPutImage(d->dpy, d->icon_pixmap, gc, ximg, 0, 0, 0, 0, w, h);
    d->icon_mask = XCreatePixmap(d->dpy, d->win, w, h, 1);
    GC gc_mask = XCreateGC(d->dpy, d->icon_mask, 0, NULL);
    XSetForeground(d->dpy, gc_mask, 1);
    XFillRectangle(d->dpy, d->icon_mask, gc_mask, 0, 0, w, h);
    XFreeGC(d->dpy, gc_mask);
    Atom net_wm_icon = XInternAtom(d->dpy, "_NET_WM_ICON", False);
    Atom cardinal = XInternAtom(d->dpy, "CARDINAL", False);
    XChangeProperty(d->dpy, d->win, net_wm_icon, cardinal, 32,
                    PropModeReplace, (unsigned char*)icon_data, 2 + w*h);
    free(icon_data);
    XWMHints *wmhints = XAllocWMHints();
    if (wmhints) {
        wmhints->icon_pixmap = d->icon_pixmap;
        wmhints->icon_mask = d->icon_mask;
        wmhints->flags = IconPixmapHint | IconMaskHint;
        XSetWMHints(d->dpy, d->win, wmhints);
        XFree(wmhints);
    }
    XDestroyImage(ximg);
    XFreeGC(d->dpy, gc);
}

/* ---------- Dibujo ---------- */
static void draw_rounded_rect(Dialog *d, int x, int y, int w, int h,
                              int r, unsigned long color, int fill) {
    XSetForeground(d->dpy, d->gc, color);
    if (fill) {
        XFillRectangle(d->dpy, d->win, d->gc, x+r, y, w-2*r, h);
        XFillRectangle(d->dpy, d->win, d->gc, x, y+r, w, h-2*r);
        XFillArc(d->dpy, d->win, d->gc, x, y, 2*r, 2*r, 90*64, 90*64);
        XFillArc(d->dpy, d->win, d->gc, x+w-2*r-1, y, 2*r, 2*r, 0, 90*64);
        XFillArc(d->dpy, d->win, d->gc, x, y+h-2*r-1, 2*r, 2*r, 180*64, 90*64);
        XFillArc(d->dpy, d->win, d->gc, x+w-2*r-1, y+h-2*r-1, 2*r, 2*r, 270*64, 90*64);
    } else {
        XDrawArc(d->dpy, d->win, d->gc, x, y, 2*r, 2*r, 90*64, 90*64);
        XDrawArc(d->dpy, d->win, d->gc, x+w-2*r-1, y, 2*r, 2*r, 0, 90*64);
        XDrawArc(d->dpy, d->win, d->gc, x, y+h-2*r-1, 2*r, 2*r, 180*64, 90*64);
        XDrawArc(d->dpy, d->win, d->gc, x+w-2*r-1, y+h-2*r-1, 2*r, 2*r, 270*64, 90*64);
        XDrawLine(d->dpy, d->win, d->gc, x+r, y, x+w-r, y);
        XDrawLine(d->dpy, d->win, d->gc, x+r, y+h, x+w-r, y+h);
        XDrawLine(d->dpy, d->win, d->gc, x, y+r, x, y+h-r);
        XDrawLine(d->dpy, d->win, d->gc, x+w, y+r, x+w, y+h-r);
    }
                              }

                              static void draw_button_bg(Dialog *d, Button *b) {
                                  int r = 6;
                                  unsigned long col_top, col_bot;
                                  if (b->pressed)      { col_top = d->btn_press; col_bot = d->btn_press; }
                                  else if (b->hover)   { col_top = d->btn_hover; col_bot = d->btn_normal; }
                                  else                 { col_top = d->btn_normal; col_bot = d->btn_normal; }
                                  draw_rounded_rect(d, b->x, b->y, b->w, b->h, r, col_top, 1);
                                  if (!b->pressed && b->hover) {
                                      for (int i = 0; i < b->h; i++) {
                                          double t = (double)i / b->h;
                                          int cr = (int)(((col_top>>16)&0xFF)*(1-t) + ((col_bot>>16)&0xFF)*t);
                                          int cg = (int)(((col_top>>8)&0xFF)*(1-t) + ((col_bot>>8)&0xFF)*t);
                                          int cb = (int)((col_top&0xFF)*(1-t) + (col_bot&0xFF)*t);
                                          XSetForeground(d->dpy, d->gc, (cr<<16)|(cg<<8)|cb);
                                          XDrawLine(d->dpy, d->win, d->gc, b->x+2, b->y+i, b->x+b->w-3, b->y+i);
                                      }
                                  }
                                  draw_rounded_rect(d, b->x, b->y, b->w, b->h, r, 0x555555, 0);
                              }

                              static void draw_button_label(Dialog *d, Button *b) {
                                  XGlyphInfo ext;
                                  XftTextExtentsUtf8(d->dpy, d->xft_font, (const FcChar8*)b->label, strlen(b->label), &ext);
                                  int tx = b->x + (b->w - ext.xOff) / 2;
                                  int ty = b->y + (b->h + d->xft_font->ascent - d->xft_font->descent) / 2;
                                  XftColor white;
                                  set_xft_color(&white, 0xFFFFFF);
                                  XftDrawStringUtf8(d->xft_draw, &white, d->xft_font, tx, ty,
                                                    (const FcChar8*)b->label, strlen(b->label));
                              }

                              static void draw_button(Dialog *d, Button *b) {
                                  draw_button_bg(d, b);
                                  draw_button_label(d, b);
                              }

                              static void draw_entry(Dialog *d) {
                                  draw_rounded_rect(d, d->entry_x, d->entry_y, d->entry_w, d->entry_h, 5, d->clr_entry_bg.pixel, 1);
                                  draw_rounded_rect(d, d->entry_x, d->entry_y, d->entry_w, d->entry_h, 5, d->clr_entry_border.pixel, 0);
                                  if (d->entry_len > 0) {
                                      int ty = d->entry_y + (d->entry_h + d->xft_font->ascent - d->xft_font->descent) / 2;
                                      XftDrawStringUtf8(d->xft_draw, &d->clr_text, d->xft_font,
                                                        d->entry_x + 8, ty, (const FcChar8*)d->entry_buf, d->entry_len);
                                  }
                                  if (d->type == DIALOG_ENTRY && !d->done) {
                                      int cx = d->entry_x + 8;
                                      if (d->entry_cursor > 0) {
                                          XGlyphInfo ext;
                                          XftTextExtentsUtf8(d->dpy, d->xft_font, (const FcChar8*)d->entry_buf, d->entry_cursor, &ext);
                                          cx += ext.xOff;
                                      }
                                      XSetForeground(d->dpy, d->gc, d->clr_text.pixel);
                                      XDrawLine(d->dpy, d->win, d->gc, cx, d->entry_y+4, cx, d->entry_y+d->entry_h-5);
                                  }
                              }

                              static void redraw(Dialog *d) {
                                  XSetForeground(d->dpy, d->gc, d->clr_bg.pixel);
                                  XFillRectangle(d->dpy, d->win, d->gc, 0, 0, d->width, d->height);

                                  if (d->text && strlen(d->text) > 0) {
                                      int y = d->xft_font->ascent + 12;
                                      char *dup = strdup(d->text);
                                      char *line = strtok(dup, "\n");
                                      while (line) {
                                          XftDrawStringUtf8(d->xft_draw, &d->clr_text, d->xft_font, 15, y, (const FcChar8*)line, strlen(line));
                                          y += d->xft_font->ascent + d->xft_font->descent + 4;
                                          line = strtok(NULL, "\n");
                                      }
                                      free(dup);
                                  }

                                  if (d->type == DIALOG_ENTRY) {
                                      draw_entry(d);
                                      draw_button(d, &d->btn_ok);
                                      draw_button(d, &d->btn_cancel);
                                  } else if (d->type == DIALOG_ERROR || d->type == DIALOG_INFO ||
                                      d->type == DIALOG_WARNING) {
                                      draw_button(d, &d->btn_ok);
                                      } else if (d->type == DIALOG_QUESTION) {
                                          draw_button(d, &d->btn_yes);
                                          draw_button(d, &d->btn_no);
                                      } else if (d->type == DIALOG_TEXT_INFO) {
                                          int area_x = 12, area_y = d->xft_font->ascent + 20;
                                          int area_w = d->width - 24 - 16;
                                          int area_h = d->height - area_y - 45;
                                          int line_h = d->xft_font->ascent + d->xft_font->descent + 2;
                                          int max_lines = area_h / line_h;
                                          int start = d->scroll_y;
                                          for (int i = 0; i < max_lines && (i+start) < d->nlines; i++) {
                                              XftDrawStringUtf8(d->xft_draw, &d->clr_text, d->xft_font,
                                                                area_x, area_y + i*line_h,
                                                                (const FcChar8*)d->lines[i+start],
                                                                strlen(d->lines[i+start]));
                                          }
                                          d->scrollbar_x = d->width - 20;
                                          d->scrollbar_w = 8;
                                          XSetForeground(d->dpy, d->gc, d->clr_scroll_bg.pixel);
                                          XFillRectangle(d->dpy, d->win, d->gc, d->scrollbar_x, area_y, d->scrollbar_w, area_h);
                                          if (d->nlines > max_lines) {
                                              double dh = area_h;
                                              double ratio = (double)max_lines / d->nlines;
                                              double thumb_h = dh * ratio;
                                              if (thumb_h < 10) thumb_h = 10;
                                              double range = dh - thumb_h;
                                              double p = (double)d->scroll_y / (d->nlines - max_lines);
                                              double thumb_y = area_y + p * range;
                                              draw_rounded_rect(d, d->scrollbar_x, (int)thumb_y, d->scrollbar_w, (int)thumb_h, 4, d->clr_scroll_thumb.pixel, 1);
                                          }
                                          draw_button(d, &d->btn_ok);
                                      }
                              }

                              static void init_buttons(Dialog *d) {
                                  int bw = 80, bh = 28, margin = 15;
                                  if (d->type == DIALOG_ENTRY || d->type == DIALOG_TEXT_INFO) {
                                      d->btn_ok.x = d->width - 2*bw - 30; d->btn_ok.y = d->height - bh - margin;
                                      d->btn_ok.w = bw; d->btn_ok.h = bh; d->btn_ok.label = "OK";
                                      d->btn_cancel.x = d->width - bw - margin; d->btn_cancel.y = d->height - bh - margin;
                                      d->btn_cancel.w = bw; d->btn_cancel.h = bh; d->btn_cancel.label = "Cancel";
                                  } else if (d->type == DIALOG_QUESTION) {
                                      d->btn_yes.x = d->width/2 - bw - 20; d->btn_yes.y = d->height - bh - margin;
                                      d->btn_yes.w = bw; d->btn_yes.h = bh; d->btn_yes.label = "Yes";
                                      d->btn_no.x = d->width/2 + 20; d->btn_no.y = d->height - bh - margin;
                                      d->btn_no.w = bw; d->btn_no.h = bh; d->btn_no.label = "No";
                                  } else {
                                      d->btn_ok.x = d->width/2 - bw/2; d->btn_ok.y = d->height - bh - margin;
                                      d->btn_ok.w = bw; d->btn_ok.h = bh; d->btn_ok.label = "OK";
                                  }
                                  if (d->type == DIALOG_ENTRY) {
                                      d->entry_x = 20; d->entry_y = 65; d->entry_w = d->width - 40; d->entry_h = 28;
                                  }
                              }

                              static void load_text(Dialog *d) {
                                  FILE *fp = stdin;
                                  if (d->filename) { fp = fopen(d->filename, "r"); if (!fp) { perror("lyntersh"); exit(1); } }
                                  d->lines = malloc(MAX_LINES * sizeof(char*));
                                  char buf[2048];
                                  d->nlines = 0;
                                  while (fgets(buf, sizeof(buf), fp) && d->nlines < MAX_LINES) {
                                      buf[strcspn(buf, "\n")] = 0;
                                      d->lines[d->nlines++] = strdup(buf);
                                  }
                                  if (fp != stdin) fclose(fp);
                              }

                              static int inside(Button *b, int x, int y) {
                                  return x >= b->x && x <= b->x+b->w && y >= b->y && y <= b->y+b->h;
                              }

                              int main(int argc, char *argv[]) {
                                  Dialog d;
                                  memset(&d, 0, sizeof(d));
                                  d.type = -1;
                                  d.title = "Lyntersh";
                                  d.wm_class = "lyntersh";
                                  d.theme = THEME_LIGHT;
                                  d.btn_normal = 0x4CAF50; d.btn_hover = 0x66BB6A; d.btn_press = 0x388E3C;

                                  for (int i = 1; i < argc; i++) {
                                      if (!strcmp(argv[i], "--entry")) d.type = DIALOG_ENTRY;
                                      else if (!strcmp(argv[i], "--error")) d.type = DIALOG_ERROR;
                                      else if (!strcmp(argv[i], "--info")) d.type = DIALOG_INFO;
                                      else if (!strcmp(argv[i], "--question")) d.type = DIALOG_QUESTION;
                                      else if (!strcmp(argv[i], "--warning")) d.type = DIALOG_WARNING;
                                      else if (!strcmp(argv[i], "--text-info")) d.type = DIALOG_TEXT_INFO;
                                      else if (!strcmp(argv[i], "--text") && i+1<argc) d.text = argv[++i];
                                      else if (!strcmp(argv[i], "--title") && i+1<argc) d.title = argv[++i];
                                      else if (!strcmp(argv[i], "--icon") && i+1<argc) d.icon_path = argv[++i];
                                      else if (!strcmp(argv[i], "--wm-class") && i+1<argc) d.wm_class = argv[++i];
                                      else if (!strcmp(argv[i], "--color") && i+1<argc) set_button_colors(&d, argv[++i]);
                                      else if (!strcmp(argv[i], "--theme") && i+1<argc) {
                                          if (strcmp(argv[i+1], "dark") == 0) d.theme = THEME_DARK;
                                          else if (strcmp(argv[i+1], "light") == 0) d.theme = THEME_LIGHT;
                                          i++;
                                      }
                                      else if (!strcmp(argv[i], "--entry-text") && i+1<argc) {
                                          strncpy(d.entry_buf, argv[++i], BUF_SIZE-1);
                                          d.entry_len = strlen(d.entry_buf); d.entry_cursor = d.entry_len;
                                      }
                                      else if (!strcmp(argv[i], "--filename") && i+1<argc) d.filename = argv[++i];
                                      else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                                          printf("Uso:\n  lyntersh [OPCIÓN…]\n\n"
                                          "Opciones de ayuda:\n"
                                          "  -h, --help               Mostrar opciones de ayuda\n"
                                          "  --help-all               Muestra todas las opciones de ayuda\n"
                                          "  --help-general           Mostrar opciones generales\n"
                                          "  --help-entry             Mostrar las opciones de la entrada de texto\n"
                                          "  --help-error             Mostrar las opciones de error\n"
                                          "  --help-info              Mostrar las opciones de información\n"
                                          "  --help-warning           Mostrar las opciones de advertencia\n"
                                          "  --help-text-info         Mostrar opciones del texto de información\n"
                                          "  --help-theme             Mostrar opciones de tema\n\n"
                                          "Opciones de la aplicación:\n"
                                          "  --entry                  Mostrar el diálogo de entrada de texto\n"
                                          "  --error                  Mostrar el diálogo de error\n"
                                          "  --info                   Mostrar el diálogo de información\n"
                                          "  --question               Mostrar el diálogo de pregunta\n"
                                          "  --warning                Mostrar el diálogo de advertencia\n"
                                          "  --text-info              Mostrar el diálogo de texto de información\n\n"
                                          "Configuraciones de la aplicación:\n"
                                          "  --title=\"TÍTULO\"          Título de la ventana\n"
                                          "  --icon=\"RUTA\"             Icono de ventana (PNG)\n"
                                          "  --wm-class=\"CLASE\"       Clase WM\n"
                                          "  --color=\"COLOR\"           Color de botones (green, red, blue, orange, purple o #RRGGBB)\n"
                                          "  --theme=\"dark|light\"      Tema visual (por defecto light)\n");
                                          return 0;
                                      }
                                      else if (!strcmp(argv[i], "--help-theme")) {
                                          printf("Opciones de tema:\n  --theme=\"light\"  Tema claro (por defecto)\n  --theme=\"dark\"   Tema oscuro\n");
                                          return 0;
                                      }
                                      else if (!strncmp(argv[i], "--help-", 7)) {
                                          printf("Ayuda para %s (implementación moderna con Xft)\n", argv[i]+7);
                                          return 0;
                                      }
                                  }
                                  if (d.type == -1) { fprintf(stderr, "Falta tipo de diálogo\n"); return 1; }
                                  if (!d.text && d.type != DIALOG_TEXT_INFO) d.text = "";

                                  d.dpy = XOpenDisplay(NULL);
                                  if (!d.dpy) { fprintf(stderr, "No se pudo conectar a X11\n"); return 1; }
                                  d.screen = DefaultScreen(d.dpy);
                                  d.vis = DefaultVisual(d.dpy, d.screen);
                                  d.depth = DefaultDepth(d.dpy, d.screen);
                                  d.black = BlackPixel(d.dpy, d.screen);
                                  d.white = WhitePixel(d.dpy, d.screen);
                                  d.cmap = DefaultColormap(d.dpy, d.screen);

                                  d.xft_font = load_xft_font(d.dpy, d.screen);
                                  if (!d.xft_font) { fprintf(stderr, "No se pudo cargar fuente Xft\n"); return 1; }

                                  apply_theme(&d);

                                  switch (d.type) {
                                      case DIALOG_ENTRY:       d.width = 420; d.height = 180; break;
                                      case DIALOG_ERROR:
                                      case DIALOG_INFO:
                                      case DIALOG_WARNING:     d.width = 360; d.height = 140; break;
                                      case DIALOG_QUESTION:    d.width = 380; d.height = 150; break;
                                      case DIALOG_TEXT_INFO:   d.width = 620; d.height = 420; break;
                                      default: d.width = 300; d.height = 120;
                                  }

                                  XSetWindowAttributes attr;
                                  attr.background_pixel = d.clr_bg.pixel;
                                  d.win = XCreateWindow(d.dpy, RootWindow(d.dpy, d.screen), 0, 0, d.width, d.height, 1,
                                                        d.depth, InputOutput, d.vis, CWBackPixel, &attr);
                                  XStoreName(d.dpy, d.win, d.title);

                                  XClassHint class_hint = {0};
                                  class_hint.res_name = d.wm_class; class_hint.res_class = d.wm_class;
                                  XSetClassHint(d.dpy, d.win, &class_hint);
                                  XSelectInput(d.dpy, d.win, ExposureMask | ButtonPressMask | ButtonReleaseMask |
                                  KeyPressMask | PointerMotionMask | StructureNotifyMask);

                                  d.cur_hand = XCreateFontCursor(d.dpy, XC_hand2);
                                  d.cur_xterm = XCreateFontCursor(d.dpy, XC_xterm);
                                  d.current_cursor = None;
                                  XDefineCursor(d.dpy, d.win, None);

                                  if (d.icon_path) load_icon(&d, d.icon_path);

                                  XMapWindow(d.dpy, d.win);
                                  d.xft_draw = XftDrawCreate(d.dpy, d.win, d.vis, d.cmap);

                                  XGCValues gcv;
                                  d.gc = XCreateGC(d.dpy, d.win, 0, &gcv);

                                  init_buttons(&d);
                                  if (d.type == DIALOG_TEXT_INFO) load_text(&d);

                                  XEvent ev;
                                  int prev_hover_ok = 0, prev_hover_cancel = 0, prev_hover_yes = 0, prev_hover_no = 0;
                                  Button *pressed_btn = NULL;

                                  while (!d.done) {
                                      XNextEvent(d.dpy, &ev);
                                      switch (ev.type) {
                                          case Expose:
                                              if (ev.xexpose.count == 0) redraw(&d);
                                              break;
                                          case MotionNotify: {
                                              int mx = ev.xmotion.x, my = ev.xmotion.y;
                                              int hov_ok = inside(&d.btn_ok, mx, my);
                                              int hov_cancel = (d.type==DIALOG_ENTRY||d.type==DIALOG_TEXT_INFO) && inside(&d.btn_cancel, mx, my);
                                              int hov_yes = (d.type==DIALOG_QUESTION) && inside(&d.btn_yes, mx, my);
                                              int hov_no  = (d.type==DIALOG_QUESTION) && inside(&d.btn_no, mx, my);
                                              int need_redraw = 0;
                                              if (hov_ok != prev_hover_ok) { d.btn_ok.hover = hov_ok; prev_hover_ok = hov_ok; need_redraw=1; }
                                              if (hov_cancel != prev_hover_cancel) { d.btn_cancel.hover = hov_cancel; prev_hover_cancel = hov_cancel; need_redraw=1; }
                                              if (hov_yes != prev_hover_yes) { d.btn_yes.hover = hov_yes; prev_hover_yes = hov_yes; need_redraw=1; }
                                              if (hov_no != prev_hover_no) { d.btn_no.hover = hov_no; prev_hover_no = hov_no; need_redraw=1; }

                                              Cursor desired = None;
                                              if (d.type == DIALOG_ENTRY) {
                                                  if (hov_ok || hov_cancel) desired = d.cur_hand;
                                                  else if (mx >= d.entry_x && mx <= d.entry_x+d.entry_w &&
                                                      my >= d.entry_y && my <= d.entry_y+d.entry_h) desired = d.cur_xterm;
                                              } else if (d.type == DIALOG_TEXT_INFO) {
                                                  if (hov_ok) desired = d.cur_hand;
                                                  else {
                                                      int area_x = 12, area_y = d.xft_font->ascent + 20;
                                                      int area_w = d.width - 24 - 16, area_h = d.height - area_y - 45;
                                                      if (mx >= area_x && mx <= area_x+area_w && my >= area_y && my <= area_y+area_h) desired = d.cur_xterm;
                                                      else if (mx >= d.scrollbar_x && mx <= d.scrollbar_x+d.scrollbar_w && my >= area_y && my <= area_y+area_h) desired = d.cur_hand;
                                                  }
                                              } else if (d.type == DIALOG_ERROR || d.type == DIALOG_INFO || d.type == DIALOG_WARNING) {
                                                  if (hov_ok) desired = d.cur_hand;
                                              } else if (d.type == DIALOG_QUESTION) {
                                                  if (hov_yes || hov_no) desired = d.cur_hand;
                                              }

                                              if (desired != d.current_cursor) { XDefineCursor(d.dpy, d.win, desired); d.current_cursor = desired; }
                                              if (need_redraw) redraw(&d);
                                              break;
                                          }
                                          case ButtonPress: {
                                              int x = ev.xbutton.x, y = ev.xbutton.y;
                                              if (ev.xbutton.button == 4 && d.type == DIALOG_TEXT_INFO) {
                                                  if (d.scroll_y > 0) { d.scroll_y--; redraw(&d); }
                                              } else if (ev.xbutton.button == 5 && d.type == DIALOG_TEXT_INFO) {
                                                  int max = d.nlines - (d.height - d.xft_font->ascent - 65) / (d.xft_font->ascent + d.xft_font->descent + 2);
                                                  if (d.scroll_y < max) { d.scroll_y++; redraw(&d); }
                                              } else if (ev.xbutton.button == 1) {
                                                  Button *target = NULL;
                                                  if (inside(&d.btn_ok, x, y)) target = &d.btn_ok;
                                                  else if (d.type==DIALOG_ENTRY||d.type==DIALOG_TEXT_INFO) {
                                                      if (inside(&d.btn_cancel, x, y)) target = &d.btn_cancel;
                                                  } else if (d.type==DIALOG_QUESTION) {
                                                      if (inside(&d.btn_yes, x, y)) target = &d.btn_yes;
                                                      else if (inside(&d.btn_no, x, y)) target = &d.btn_no;
                                                  }
                                                  if (target) { target->pressed = 1; pressed_btn = target; redraw(&d); }
                                              }
                                              break;
                                          }
                                          case ButtonRelease: {
                                              if (ev.xbutton.button == 1 && pressed_btn) {
                                                  pressed_btn->pressed = 0;
                                                  int x = ev.xbutton.x, y = ev.xbutton.y;
                                                  if (inside(pressed_btn, x, y)) {
                                                      if (pressed_btn == &d.btn_ok)        { d.done=1; d.result=0; }
                                                      else if (pressed_btn == &d.btn_cancel || pressed_btn == &d.btn_no) { d.done=1; d.result=1; }
                                                      else if (pressed_btn == &d.btn_yes)  { d.done=1; d.result=0; }
                                                  }
                                                  pressed_btn = NULL; redraw(&d);
                                              }
                                              break;
                                          }
                                          case KeyPress: {
                                              char buf[8] = {0};
                                              KeySym ks;
                                              int len = XLookupString(&ev.xkey, buf, sizeof(buf), &ks, NULL);
                                              if (d.type == DIALOG_ENTRY) {
                                                  if (ks == XK_Return || ks == XK_KP_Enter) { d.done=1; d.result=0; }
                                                  else if (ks == XK_BackSpace) {
                                                      if (d.entry_cursor > 0) {
                                                          int prev = d.entry_cursor - 1;
                                                          while (prev > 0 && (d.entry_buf[prev] & 0xC0) == 0x80) prev--;
                                                          memmove(&d.entry_buf[prev], &d.entry_buf[d.entry_cursor], d.entry_len - d.entry_cursor);
                                                          d.entry_len -= (d.entry_cursor - prev);
                                                          d.entry_cursor = prev;
                                                          d.entry_buf[d.entry_len] = '\0';
                                                          redraw(&d);
                                                      }
                                                  } else if (ks == XK_Delete && d.entry_cursor < d.entry_len) {
                                                      int next = d.entry_cursor + 1;
                                                      while (next < d.entry_len && (d.entry_buf[next] & 0xC0) == 0x80) next++;
                                                      memmove(&d.entry_buf[d.entry_cursor], &d.entry_buf[next], d.entry_len - next);
                                                      d.entry_len -= (next - d.entry_cursor);
                                                      d.entry_buf[d.entry_len] = '\0';
                                                      redraw(&d);
                                                  } else if (ks == XK_Left && d.entry_cursor > 0) {
                                                      int prev = d.entry_cursor - 1;
                                                      while (prev > 0 && (d.entry_buf[prev] & 0xC0) == 0x80) prev--;
                                                      d.entry_cursor = prev;
                                                      redraw(&d);
                                                  } else if (ks == XK_Right && d.entry_cursor < d.entry_len) {
                                                      int next = d.entry_cursor + 1;
                                                      while (next < d.entry_len && (d.entry_buf[next] & 0xC0) == 0x80) next++;
                                                      d.entry_cursor = next;
                                                      redraw(&d);
                                                  } else if (len > 0 && d.entry_len + len < BUF_SIZE-1) {
                                                      memmove(&d.entry_buf[d.entry_cursor+len], &d.entry_buf[d.entry_cursor], d.entry_len - d.entry_cursor);
                                                      memcpy(&d.entry_buf[d.entry_cursor], buf, len);
                                                      d.entry_cursor += len;
                                                      d.entry_len += len;
                                                      d.entry_buf[d.entry_len] = '\0';
                                                      redraw(&d);
                                                  }
                                              } else if (d.type == DIALOG_TEXT_INFO) {
                                                  if (ks == XK_Up) { if (d.scroll_y > 0) d.scroll_y--; redraw(&d); }
                                                  else if (ks == XK_Down) {
                                                      int max = d.nlines - (d.height - d.xft_font->ascent - 65) / (d.xft_font->ascent + d.xft_font->descent + 2);
                                                      if (d.scroll_y < max) d.scroll_y++;
                                                      redraw(&d);
                                                  }
                                              }
                                              break;
                                          }
                                          case ClientMessage: d.done = 1; d.result = 1; break;
                                      }
                                  }

                                  if (d.type == DIALOG_ENTRY && d.result == 0) printf("%s\n", d.entry_buf);

                                  XftDrawDestroy(d.xft_draw);
                                  XftFontClose(d.dpy, d.xft_font);
                                  if (d.icon_pixmap != None) XFreePixmap(d.dpy, d.icon_pixmap);
                                  if (d.icon_mask != None) XFreePixmap(d.dpy, d.icon_mask);
                                  XFreeCursor(d.dpy, d.cur_hand);
                                  XFreeCursor(d.dpy, d.cur_xterm);
                                  XFreeGC(d.dpy, d.gc);
                                  XDestroyWindow(d.dpy, d.win);
                                  XCloseDisplay(d.dpy);
                                  return d.result;
                              }
