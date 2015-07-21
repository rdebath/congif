#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <float.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "term.h"
#include "mbf.h"
#include "gif.h"

void
parse_script(Term *term, const char *timing, const char *dialogue)
{
    FILE *ft;
    int fd;
    float t;
    int n;
    uint8_t ch;

    ft = fopen(timing, "r");
    fd = open(dialogue, O_RDONLY);
    if (ft == NULL || fd == -1)
        return;
    /* discard first line of dialogue */
    do read(fd, &ch, 1); while (ch != '\n');
    while (fscanf(ft, "%f %d\n", &t, &n) == 2) {
        while (n--) {
            read(fd, &ch, 1);
            parse(term, ch);
        }
    }
    close(fd);
    fclose(ft);
}

int
get_index(Font *font, uint16_t code)
{
    int index;

    index = search_glyph(font, code);
    if (index == -1)
        index = search_glyph(font, 0xFFFD);
    if (index == -1)
        index = search_glyph(font, 0x003F);
    if (index == -1)
        index = search_glyph(font, 0x0020);
    if (index == -1)
        index = search_glyph(font, 0x0000);
    return index;
}

uint8_t
get_pair(Term *term, int row, int col)
{
    Cell cell;
    uint8_t fore, back;
    int inverse;

    /* TODO: add support for A_INVISIBLE */
    inverse = term->mode & M_REVERSE;
    if (term->mode & M_CURSORVIS)
        inverse = term->row == row && term->col == col ? !inverse : inverse;
    cell = term->addr[row][col];
    inverse = cell.attr & A_INVERSE ? !inverse : inverse;
    if (inverse) {
        fore = cell.pair & 0xF;
        back = cell.pair >> 4;
    } else {
        fore = cell.pair >> 4;
        back = cell.pair & 0xF;
    }
    if (cell.attr & (A_DIM | A_UNDERLINE))
        fore = 0x6;
    else if (cell.attr & (A_ITALIC | A_CROSSED))
        fore = 0x2;
    if (cell.attr & A_BOLD)
        fore |= 0x8;
    return (fore << 4) | (back & 0xF);
}

void
draw_char(Font *font, GIF *gif, uint16_t code, uint8_t pair, int row, int col)
{
    int i, j;
    int x, y;
    int index;
    int pixel;
    uint8_t *strip;

    index = get_index(font, code);
    if (index == -1)
        return;
    strip = &font->data[font->stride * font->header.h * index];
    y = font->header.h * row;
    for (i = 0; i < font->header.h; i++) {
        x = font->header.w * col;
        for (j = 0; j < font->header.w; j++) {
            pixel = strip[j >> 3] & (1 << (7 - (j & 7)));
            gif->cur[y * gif->w + x] = pixel ? pair >> 4 : pair & 0xF;
            x++;
        }
        y++;
        strip += font->stride;
    }
}

void
render(Term *term, Font *font, GIF *gif, uint16_t delay)
{
    int i, j;
    uint16_t code;
    uint8_t pair;

    for (i = 0; i < term->rows; i++) {
        for (j = 0; j < term->cols; j++) {
            code = term->addr[i][j].code;
            pair = get_pair(term, i, j);
            draw_char(font, gif, code, pair, i, j);
        }
    }
    add_frame(gif, delay);
}

int
convert_script(Term *term, const char *timing, const char *dialogue,
               const char *mbf, const char *anim, float div, float max,
               int cur)
{
    FILE *ft;
    int fd;
    float t;
    int n;
    uint8_t ch;
    Font *font;
    int w, h;
    int i;
    float d;
    GIF *gif;

    ft = fopen(timing, "r");
    if (!ft) {
        fprintf(stderr, "error: could not load timings: %s\n", timing);
        goto no_ft;
    }
    fd = open(dialogue, O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "error: could not load dialogue: %s\n", dialogue);
        goto no_fd;
    }
    font = load_font(mbf);
    if (!font) {
        fprintf(stderr, "error: could not load font: %s\n", mbf);
        goto no_font;
    }
    w = term->cols * font->header.w;
    h = term->rows * font->header.h;
    gif = new_gif(anim, w, h, term->plt);
    if (!gif) {
        fprintf(stderr, "error: could not create GIF: %s\n", anim);
        goto no_gif;
    }
    /* discard first line of dialogue */
    do read(fd, &ch, 1); while (ch != '\n');
    i = 0;
    d = 0;
    while (fscanf(ft, "%f %d\n", &t, &n) == 2) {
        d += ((t > max ? max : t) * 100.0 / div);
        if (i && d > 5) {
            printf("\r#%d", i);
            fflush(stdout);
            render(term, font, gif, (uint16_t) (d + 0.5));
            d = 0;
        }
        while (n--) {
            read(fd, &ch, 1);
            parse(term, ch);
        }
        if (!cur)
            term->mode &= ~M_CURSORVIS;
        i++;
    }
    printf("\r#%d\n", i);
    render(term, font, gif, 0);
    close_gif(gif);
    return 0;
no_gif:
    free(font);
no_font:
    close(fd);
no_fd:
    fclose(ft);
no_ft:
    return 1;
}

void
help(char *name)
{
    fprintf(stderr,
        "Usage: %s [OPTIONS] timings dialogue\n\n"
        "OPTIONS:\n"
        "  -w columns   Terminal width\n"
        "  -h lines     Terminal height\n"
        "  -f font      File name of MBF font to use\n"
        "  -o output    File name of GIF output\n"
        "  -d divisor   Speedup, as in scriptreplay(1)\n"
        "  -m maxdelay  Maximum delay, as in scriptreplay(1)\n"
        "  -c on|off    Show/hide cursor\n\n"
        "timings:       File generated by script(1)'s -t option\n"
        "dialogue:      File generated by script(1)'s regular output\n"
    , name);
}

int
main(int argc, char *argv[])
{
    int opt;
    int w, h;
    char *f;
    char *o;
    float d, m;
    char *t;
    char *s;
    int c;
    int ret;
    Term *term;

    w = 80; h = 30;
    f = "6x11.mbf";
    o = "con.gif";
    d = 1.0; m = FLT_MAX;
    c = 1;
    while ((opt = getopt(argc, argv, "w:h:f:o:d:m:c:")) != -1) {
        switch (opt) {
        case 'w':
            w = atoi(optarg);
            break;
        case 'h':
            h = atoi(optarg);
            break;
        case 'f':
            f = optarg;
            break;
        case 'o':
            o = optarg;
            break;
        case 'd':
            d = atof(optarg);
            break;
        case 'm':
            m = atof(optarg);
            break;
        case 'c':
            if (!strcmp(optarg, "on") || !strcmp(optarg, "1"))
                c = 1;
            else if (!strcmp(optarg, "off") || !strcmp(optarg, "0"))
                c = 0;
            break;
        default:
            help(argv[0]);
            return 1;
        }
    }
    if (optind >= argc - 1) {
        fprintf(stderr, "%s: no input given\n", argv[0]);
        help(argv[0]);
        return 1;
    }
    t = argv[optind++];
    s = argv[optind++];
    term = new_term(h, w);
    ret = convert_script(term, t, s, f, o, d, m, c);
    free(term);
    return ret;
}
