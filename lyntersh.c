/*
 * Lyntersh - Zenity moderno con Xlib pura, cursores contextuales, título/icono personalizado
 * Compilar: gcc -o lyntersh lyntersh.c -lX11 -lm -O2
*/

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#define BUF_SIZE 1024
#define MAX_LINES 500

/* Paleta verde */
#define CLR_BG       0xE8E8E8
#define CLR_BTN      0x4CAF50
#define CLR_BTN_HOV  0x66BB6A
#define CLR_BTN_PRS  0x388E3C
#define CLR_BTN_TXT  0xFFFFFF
#define CLR_ENTRY_BG 0xFFFFFF
#define CLR_TEXT     0x000000
#define CLR_SCROLL   0xB0B0B0

typedef enum {
    DIALOG_ENTRY, DIALOG_ERROR, DIALOG_INFO,
    DIALOG_QUESTION, DIALOG_WARNING, DIALOG_TEXT_INFO
} DialogType;

typedef struct {
    int x, y, w, h;
    int hover, pressed;
    const char *label;
} Button;

typedef struct {
    Display *dpy;
    Window win;
    GC gc;
    XFontStruct *font;
    int screen;
    Colormap cmap;
    unsigned long black, white;
    int width, height;
    DialogType type;
    char *title, *text, *filename;
    char *icon_path;
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
    /* icono */
    Pixmap icon_pixmap;
    Pixmap icon_mask;
} Dialog;

/* Fuente moderna */
static XFontStruct* load_nice_font(Display *dpy) {
    const char *fonts[] = {
        "-*-helvetica-*-r-*-*-11-*-*-*-*-*-*-*",
        "-*-liberation sans-*-r-*-*-11-*-*-*-*-*-*-*",
        "-*-dejavu sans-*-r-*-*-11-*-*-*-*-*-*-*",
        "-*-sans-*-r-*-*-11-*-*-*-*-*-*-*",
        "fixed"
    };
    for (int i = 0; i < 5; i++) {
        XFontStruct *f = XLoadQueryFont(dpy, fonts[i]);
        if (f) return f;
    }
    return NULL;
}

/* Rectángulo redondeado */
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
                                  if (b->pressed)      { col_top = CLR_BTN_PRS; col_bot = CLR_BTN_PRS; }
                                  else if (b->hover)   { col_top = CLR_BTN_HOV; col_bot = CLR_BTN;   }
                                  else                 { col_top = CLR_BTN;     col_bot = CLR_BTN;   }

                                  draw_rounded_rect(d, b->x, b->y, b->w, b->h, r, col_top, 1);
                                  if (!b->pressed && b->hover) {
                                      for (int i = 0; i < b->h; i++) {
                                          double t = (double)i / b->h;
                                          int cr = (int)(((col_top>>16)&0xFF) * (1-t) + ((col_bot>>16)&0xFF) * t);
                                          int cg = (int)(((col_top>>8)&0xFF) * (1-t) + ((col_bot>>8)&0xFF) * t);
                                          int cb = (int)((col_top&0xFF) * (1-t) + (col_bot&0xFF) * t);
                                          unsigned long c = (cr<<16)|(cg<<8)|cb;
                                          XSetForeground(d->dpy, d->gc, c);
                                          XDrawLine(d->dpy, d->win, d->gc, b->x+2, b->y+i, b->x+b->w-3, b->y+i);
                                      }
                                  }
                                  draw_rounded_rect(d, b->x, b->y, b->w, b->h, r, 0x555555, 0);
                              }

                              static void draw_button(Dialog *d, Button *b) {
                                  draw_button_bg(d, b);
                                  XSetForeground(d->dpy, d->gc, CLR_BTN_TXT);
                                  int tw = XTextWidth(d->font, b->label, strlen(b->label));
                                  int tx = b->x + (b->w - tw)/2;
                                  int ty = b->y + (b->h + d->font->ascent - d->font->descent)/2;
                                  XDrawString(d->dpy, d->win, d->gc, tx, ty, b->label, strlen(b->label));
                              }

                              static void draw_entry(Dialog *d) {
                                  int r = 5;
                                  draw_rounded_rect(d, d->entry_x, d->entry_y, d->entry_w, d->entry_h, r, CLR_ENTRY_BG, 1);
                                  draw_rounded_rect(d, d->entry_x, d->entry_y, d->entry_w, d->entry_h, r, 0x888888, 0);

                                  XSetForeground(d->dpy, d->gc, CLR_TEXT);
                                  if (d->entry_len > 0) {
                                      int ty = d->entry_y + (d->entry_h + d->font->ascent - d->font->descent)/2;
                                      XDrawString(d->dpy, d->win, d->gc, d->entry_x + 8, ty,
                                                  d->entry_buf, d->entry_len);
                                  }
                                  if (d->type == DIALOG_ENTRY && !d->done) {
                                      int cx = d->entry_x + 8 + XTextWidth(d->font, d->entry_buf, d->entry_cursor);
                                      XDrawLine(d->dpy, d->win, d->gc, cx, d->entry_y+4, cx, d->entry_y+d->entry_h-5);
                                  }
                              }

                              static void redraw(Dialog *d) {
                                  XSetForeground(d->dpy, d->gc, CLR_BG);
                                  XFillRectangle(d->dpy, d->win, d->gc, 0, 0, d->width, d->height);

                                  XSetForeground(d->dpy, d->gc, CLR_TEXT);
                                  if (d->text && strlen(d->text) > 0) {
                                      int y = d->font->ascent + 12;
                                      char *dup = strdup(d->text);
                                      char *line = strtok(dup, "\n");
                                      while (line) {
                                          XDrawString(d->dpy, d->win, d->gc, 15, y, line, strlen(line));
                                          y += d->font->ascent + d->font->descent + 4;
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
                                          int area_x = 12, area_y = d->font->ascent + 20;
                                          int area_w = d->width - 24 - 16;
                                          int area_h = d->height - area_y - 45;
                                          int line_h = d->font->ascent + d->font->descent + 2;
                                          XSetForeground(d->dpy, d->gc, CLR_TEXT);
                                          int max_lines = area_h / line_h;
                                          int start = d->scroll_y;
                                          for (int i = 0; i < max_lines && (i+start) < d->nlines; i++) {
                                              XDrawString(d->dpy, d->win, d->gc, area_x, area_y + i*line_h,
                                                          d->lines[i+start], strlen(d->lines[i+start]));
                                          }
                                          /* Scrollbar */
                                          d->scrollbar_x = d->width - 20;
                                          d->scrollbar_w = 8;
                                          XSetForeground(d->dpy, d->gc, 0xCCCCCC);
                                          XFillRectangle(d->dpy, d->win, d->gc, d->scrollbar_x, area_y, d->scrollbar_w, area_h);
                                          if (d->nlines > max_lines) {
                                              double dh = area_h;
                                              double ratio = (double)max_lines / d->nlines;
                                              double thumb_h = dh * ratio;
                                              if (thumb_h < 10) thumb_h = 10;
                                              double range = dh - thumb_h;
                                              double p = (double)d->scroll_y / (d->nlines - max_lines);
                                              double thumb_y = area_y + p * range;
                                              d->scrollbar_h = thumb_h;
                                              d->scrollbar_y = thumb_y;
                                              draw_rounded_rect(d, d->scrollbar_x, (int)thumb_y, d->scrollbar_w, (int)thumb_h, 4, CLR_SCROLL, 1);
                                          }
                                          draw_button(d, &d->btn_ok);
                                      }
                              }

                              static void init_buttons(Dialog *d) {
                                  int bw = 80, bh = 28, margin = 15;
                                  if (d->type == DIALOG_ENTRY || d->type == DIALOG_TEXT_INFO) {
                                      d->btn_ok.x = d->width - 2*bw - 30;
                                      d->btn_ok.y = d->height - bh - margin;
                                      d->btn_ok.w = bw; d->btn_ok.h = bh;
                                      d->btn_ok.label = "OK";
                                      d->btn_cancel.x = d->width - bw - margin;
                                      d->btn_cancel.y = d->height - bh - margin;
                                      d->btn_cancel.w = bw; d->btn_cancel.h = bh;
                                      d->btn_cancel.label = "Cancel";
                                  } else if (d->type == DIALOG_QUESTION) {
                                      d->btn_yes.x = d->width/2 - bw - 20;
                                      d->btn_yes.y = d->height - bh - margin;
                                      d->btn_yes.w = bw; d->btn_yes.h = bh;
                                      d->btn_yes.label = "Yes";
                                      d->btn_no.x = d->width/2 + 20;
                                      d->btn_no.y = d->height - bh - margin;
                                      d->btn_no.w = bw; d->btn_no.h = bh;
                                      d->btn_no.label = "No";
                                  } else {
                                      d->btn_ok.x = d->width/2 - bw/2;
                                      d->btn_ok.y = d->height - bh - margin;
                                      d->btn_ok.w = bw; d->btn_ok.h = bh;
                                      d->btn_ok.label = "OK";
                                  }
                                  if (d->type == DIALOG_ENTRY) {
                                      d->entry_x = 20;
                                      d->entry_y = 65;
                                      d->entry_w = d->width - 40;
                                      d->entry_h = 28;
                                  }
                              }

                              static void load_text(Dialog *d) {
                                  FILE *fp = stdin;
                                  if (d->filename) {
                                      fp = fopen(d->filename, "r");
                                      if (!fp) { perror("lyntersh"); exit(1); }
                                  }
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

                              /* Cargar icono desde un archivo PPM binario (P6) */
                              static void load_window_icon(Dialog *d, const char *path) {
                                  FILE *fp = fopen(path, "rb");
                                  if (!fp) {
                                      fprintf(stderr, "No se pudo abrir el icono '%s'\n", path);
                                      return;
                                  }
                                  char magic[3];
                                  if (!fgets(magic, sizeof(magic), fp) || strncmp(magic, "P6", 2)) {
                                      fprintf(stderr, "El icono debe ser un PPM binario (P6)\n");
                                      fclose(fp);
                                      return;
                                  }
                                  /* Saltar comentarios */
                                  int c = getc(fp);
                                  while (c == '#') {
                                      while (getc(fp) != '\n');
                                      c = getc(fp);
                                  }
                                  ungetc(c, fp);
                                  int w, h, maxval;
                                  if (fscanf(fp, "%d %d %d", &w, &h, &maxval) != 3 || maxval != 255) {
                                      fprintf(stderr, "Formato PPM inválido\n");
                                      fclose(fp);
                                      return;
                                  }
                                  fgetc(fp); /* el espacio/blanco después del maxval */

                                  unsigned char *data = malloc(w * h * 3);
                                  if (fread(data, 3, w * h, fp) != (size_t)(w * h)) {
                                      fprintf(stderr, "Error al leer los datos del icono\n");
                                      free(data);
                                      fclose(fp);
                                      return;
                                  }
                                  fclose(fp);

                                  /* Crear imagen X */
                                  Visual *vis = DefaultVisual(d->dpy, d->screen);
                                  XImage *ximg = XCreateImage(d->dpy, vis, DefaultDepth(d->dpy, d->screen),
                                                              ZPixmap, 0, NULL, w, h, 32, 0);
                                  if (!ximg) {
                                      free(data);
                                      return;
                                  }
                                  ximg->data = malloc(ximg->bytes_per_line * h);
                                  /* Convertir RGB a formato de pixel de 32 bits */
                                  for (int y = 0; y < h; y++) {
                                      for (int x = 0; x < w; x++) {
                                          unsigned char *src = data + (y * w + x) * 3;
                                          unsigned char r = src[0], g = src[1], b = src[2];
                                          unsigned long pixel = (r << 16) | (g << 8) | b;
                                          XPutPixel(ximg, x, y, pixel);
                                      }
                                  }
                                  free(data);

                                  /* Crear pixmap y máscara */
                                  GC gc_icon = XCreateGC(d->dpy, d->win, 0, NULL);
                                  d->icon_pixmap = XCreatePixmap(d->dpy, d->win, w, h, DefaultDepth(d->dpy, d->screen));
                                  XPutImage(d->dpy, d->icon_pixmap, gc_icon, ximg, 0, 0, 0, 0, w, h);
                                  d->icon_mask = XCreatePixmap(d->dpy, d->win, w, h, 1);
                                  /* máscara: todo opaco */
                                  XSetForeground(d->dpy, gc_icon, 1);
                                  XFillRectangle(d->dpy, d->icon_mask, gc_icon, 0, 0, w, h);

                                  /* Establecer icono mediante _NET_WM_ICON (más moderno) */
                                  Atom net_wm_icon = XInternAtom(d->dpy, "_NET_WM_ICON", False);
                                  Atom cardinal = XInternAtom(d->dpy, "CARDINAL", False);
                                  unsigned long *icon_data = malloc((2 + w * h) * sizeof(unsigned long));
                                  icon_data[0] = w;
                                  icon_data[1] = h;
                                  for (int y = 0; y < h; y++)
                                      for (int x = 0; x < w; x++)
                                          icon_data[2 + y*w + x] = XGetPixel(ximg, x, y);
                                  XChangeProperty(d->dpy, d->win, net_wm_icon, cardinal, 32,
                                                  PropModeReplace, (unsigned char*)icon_data, 2 + w*h);
                                  free(icon_data);

                                  /* También ponemos el icono clásico con XSetWMHints */
                                  XWMHints *wmhints = XAllocWMHints();
                                  if (wmhints) {
                                      wmhints->icon_pixmap = d->icon_pixmap;
                                      wmhints->icon_mask = d->icon_mask;
                                      wmhints->flags = IconPixmapHint | IconMaskHint;
                                      XSetWMHints(d->dpy, d->win, wmhints);
                                      XFree(wmhints);
                                  }

                                  XDestroyImage(ximg);
                                  XFreeGC(d->dpy, gc_icon);
                              }

                              int main(int argc, char *argv[]) {
                                  Dialog d;
                                  memset(&d, 0, sizeof(d));
                                  d.type = -1;
                                  d.title = "Lyntersh";
                                  d.icon_path = NULL;
                                  d.icon_pixmap = None;
                                  d.icon_mask = None;
                                  d.result = 1;

                                  for (int i = 1; i < argc; i++) {
                                      if (!strcmp(argv[i], "--entry")) d.type = DIALOG_ENTRY;
                                      else if (!strcmp(argv[i], "--error")) d.type = DIALOG_ERROR;
                                      else if (!strcmp(argv[i], "--info")) d.type = DIALOG_INFO;
                                      else if (!strcmp(argv[i], "--question")) d.type = DIALOG_QUESTION;
                                      else if (!strcmp(argv[i], "--warning")) d.type = DIALOG_WARNING;
                                      else if (!strcmp(argv[i], "--text-info")) d.type = DIALOG_TEXT_INFO;
                                      else if (!strcmp(argv[i], "--text") && i+1<argc) d.text = argv[++i];
                                      else if (!strcmp(argv[i], "--title") && i+1<argc) d.title = argv[++i];
                                      else if (!strcmp(argv[i], "--window-name") && i+1<argc) d.title = argv[++i];
                                      else if (!strcmp(argv[i], "--window-icon") && i+1<argc) d.icon_path = argv[++i];
                                      else if (!strcmp(argv[i], "--entry-text") && i+1<argc) {
                                          strncpy(d.entry_buf, argv[++i], BUF_SIZE-1);
                                          d.entry_len = strlen(d.entry_buf);
                                          d.entry_cursor = d.entry_len;
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
                                          "  --help-text-info         Mostrar opciones del texto de información\n\n"
                                          "Opciones de la aplicación:\n"
                                          "  --entry                  Mostrar el diálogo de entrada de texto\n"
                                          "  --error                  Mostrar el diálogo de error\n"
                                          "  --info                   Mostrar el diálogo de información\n"
                                          "  --question               Mostrar el diálogo de pregunta\n"
                                          "  --warning                Mostrar el diálogo de advertencia\n"
                                          "  --text-info              Mostrar el diálogo de texto de información\n"
                                          "  --window-name=\"NOMBRE\"   Título de la ventana\n"
                                          "  --window-icon=\"RUTA\"     Icono de ventana (PPM binario P6)\n");
                                          return 0;
                                      }
                                      else if (!strncmp(argv[i], "--help-", 7)) {
                                          printf("Ayuda para %s (implementación moderna)\n", argv[i]+7);
                                          return 0;
                                      }
                                  }
                                  if (d.type == -1) { fprintf(stderr, "Falta tipo de diálogo\n"); return 1; }
                                  if (!d.text && d.type != DIALOG_TEXT_INFO) d.text = "";

                                  d.dpy = XOpenDisplay(NULL);
                                  if (!d.dpy) { fprintf(stderr, "No se pudo conectar a X11\n"); return 1; }
                                  d.screen = DefaultScreen(d.dpy);
                                  d.black = BlackPixel(d.dpy, d.screen);
                                  d.white = WhitePixel(d.dpy, d.screen);
                                  d.cmap = DefaultColormap(d.dpy, d.screen);

                                  d.font = load_nice_font(d.dpy);
                                  if (!d.font) { fprintf(stderr, "No se encontró fuente\n"); return 1; }

                                  switch (d.type) {
                                      case DIALOG_ENTRY:       d.width = 420; d.height = 180; break;
                                      case DIALOG_ERROR:
                                      case DIALOG_INFO:
                                      case DIALOG_WARNING:     d.width = 360; d.height = 140; break;
                                      case DIALOG_QUESTION:    d.width = 380; d.height = 150; break;
                                      case DIALOG_TEXT_INFO:   d.width = 620; d.height = 420; break;
                                      default: d.width = 300; d.height = 120;
                                  }

                                  d.win = XCreateSimpleWindow(d.dpy, RootWindow(d.dpy, d.screen),
                                                              0, 0, d.width, d.height, 1, d.black, CLR_BG);
                                  XStoreName(d.dpy, d.win, d.title);
                                  XSelectInput(d.dpy, d.win, ExposureMask | ButtonPressMask | ButtonReleaseMask |
                                  KeyPressMask | PointerMotionMask | StructureNotifyMask);

                                  /* Cursores */
                                  d.cur_hand   = XCreateFontCursor(d.dpy, XC_hand2);
                                  d.cur_xterm  = XCreateFontCursor(d.dpy, XC_xterm);
                                  d.current_cursor = None;
                                  XDefineCursor(d.dpy, d.win, None);

                                  /* Cargar icono si se especificó */
                                  if (d.icon_path)
                                      load_window_icon(&d, d.icon_path);

                                  XMapWindow(d.dpy, d.win);

                                  XGCValues gcv;
                                  gcv.font = d.font->fid;
                                  d.gc = XCreateGC(d.dpy, d.win, GCFont, &gcv);

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
                                                  if (hov_ok || hov_cancel)
                                                      desired = d.cur_hand;
                                                  else if (mx >= d.entry_x && mx <= d.entry_x+d.entry_w &&
                                                      my >= d.entry_y && my <= d.entry_y+d.entry_h)
                                                      desired = d.cur_xterm;
                                              } else if (d.type == DIALOG_TEXT_INFO) {
                                                  if (hov_ok)
                                                      desired = d.cur_hand;
                                                  else {
                                                      int area_x = 12, area_y = d.font->ascent + 20;
                                                      int area_w = d.width - 24 - 16;
                                                      int area_h = d.height - area_y - 45;
                                                      if (mx >= area_x && mx <= area_x+area_w &&
                                                          my >= area_y && my <= area_y+area_h)
                                                          desired = d.cur_xterm;
                                                      else if (mx >= d.scrollbar_x && mx <= d.scrollbar_x+d.scrollbar_w &&
                                                          my >= area_y && my <= area_y+area_h)
                                                          desired = d.cur_hand;
                                                  }
                                              } else if (d.type == DIALOG_ERROR || d.type == DIALOG_INFO || d.type == DIALOG_WARNING) {
                                                  if (hov_ok) desired = d.cur_hand;
                                              } else if (d.type == DIALOG_QUESTION) {
                                                  if (hov_yes || hov_no) desired = d.cur_hand;
                                              }

                                              if (desired != d.current_cursor) {
                                                  XDefineCursor(d.dpy, d.win, desired);
                                                  d.current_cursor = desired;
                                              }
                                              if (need_redraw) redraw(&d);
                                              break;
                                          }
                                          case ButtonPress: {
                                              int x = ev.xbutton.x, y = ev.xbutton.y;
                                              if (ev.xbutton.button == 4 && d.type == DIALOG_TEXT_INFO) {
                                                  if (d.scroll_y > 0) { d.scroll_y--; redraw(&d); }
                                              } else if (ev.xbutton.button == 5 && d.type == DIALOG_TEXT_INFO) {
                                                  int max = d.nlines - (d.height - d.font->ascent - 65) / (d.font->ascent + d.font->descent + 2);
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
                                                  if (target) {
                                                      target->pressed = 1;
                                                      pressed_btn = target;
                                                      redraw(&d);
                                                  }
                                              }
                                              break;
                                          }
                                          case ButtonRelease: {
                                              if (ev.xbutton.button == 1 && pressed_btn) {
                                                  pressed_btn->pressed = 0;
                                                  int x = ev.xbutton.x, y = ev.xbutton.y;
                                                  if (inside(pressed_btn, x, y)) {
                                                      if (pressed_btn == &d.btn_ok)        { d.done=1; d.result=0; }
                                                      else if (pressed_btn == &d.btn_cancel ||
                                                          pressed_btn == &d.btn_no)   { d.done=1; d.result=1; }
                                                          else if (pressed_btn == &d.btn_yes)  { d.done=1; d.result=0; }
                                                  }
                                                  pressed_btn = NULL;
                                                  redraw(&d);
                                              }
                                              break;
                                          }
                                          case KeyPress: {
                                              char buf[2] = {0};
                                              KeySym ks;
                                              XLookupString(&ev.xkey, buf, 1, &ks, NULL);
                                              if (d.type == DIALOG_ENTRY) {
                                                  if (ks == XK_Return || ks == XK_KP_Enter) { d.done=1; d.result=0; }
                                                  else if (ks == XK_BackSpace) {
                                                      if (d.entry_cursor > 0) {
                                                          memmove(&d.entry_buf[d.entry_cursor-1], &d.entry_buf[d.entry_cursor], d.entry_len - d.entry_cursor);
                                                          d.entry_len--; d.entry_cursor--;
                                                          redraw(&d);
                                                      }
                                                  } else if (ks == XK_Delete && d.entry_cursor < d.entry_len) {
                                                      memmove(&d.entry_buf[d.entry_cursor], &d.entry_buf[d.entry_cursor+1], d.entry_len - d.entry_cursor - 1);
                                                      d.entry_len--;
                                                      redraw(&d);
                                                  } else if (ks == XK_Left && d.entry_cursor > 0) { d.entry_cursor--; redraw(&d); }
                                                  else if (ks == XK_Right && d.entry_cursor < d.entry_len) { d.entry_cursor++; redraw(&d); }
                                                  else if (buf[0] >= 32 && buf[0] < 127 && d.entry_len < BUF_SIZE-1) {
                                                      memmove(&d.entry_buf[d.entry_cursor+1], &d.entry_buf[d.entry_cursor], d.entry_len - d.entry_cursor);
                                                      d.entry_buf[d.entry_cursor] = buf[0];
                                                      d.entry_cursor++; d.entry_len++;
                                                      redraw(&d);
                                                  }
                                              } else if (d.type == DIALOG_TEXT_INFO) {
                                                  if (ks == XK_Up) { if(d.scroll_y>0) d.scroll_y--; redraw(&d); }
                                                  else if (ks == XK_Down) {
                                                      int max = d.nlines - (d.height - d.font->ascent - 65) / (d.font->ascent + d.font->descent + 2);
                                                      if (d.scroll_y < max) d.scroll_y++;
                                                      redraw(&d);
                                                  }
                                              }
                                              break;
                                          }
                                          case ClientMessage:
                                              d.done = 1; d.result = 1;
                                              break;
                                      }
                                  }

                                  if (d.type == DIALOG_ENTRY && d.result == 0)
                                      printf("%s\n", d.entry_buf);

                                  /* Limpieza */
                                  if (d.icon_pixmap != None) XFreePixmap(d.dpy, d.icon_pixmap);
                                  if (d.icon_mask != None) XFreePixmap(d.dpy, d.icon_mask);
                                  XFreeCursor(d.dpy, d.cur_hand);
                                  XFreeCursor(d.dpy, d.cur_xterm);
                                  XFreeGC(d.dpy, d.gc);
                                  XUnloadFont(d.dpy, d.font->fid);
                                  XDestroyWindow(d.dpy, d.win);
                                  XCloseDisplay(d.dpy);
                                  return d.result;
                              }
