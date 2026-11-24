/* options.c - see options.h */
#include "options.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void options_defaults(LaleOptions *o)
{
    memset(o, 0, sizeof *o);
    o->cd_play_seconds = 15;
    snprintf(o->edition, sizeof o->edition, "auto");
    snprintf(o->video, sizeof o->video, "4k");
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    if (e - s >= 2 && s[0] == '"' && e[-1] == '"') { e[-1] = 0; s++; }
    return s;
}

static int truthy(const char *v)
{
    return !strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "yes") || !strcmp(v, "on");
}

static int set_key(LaleOptions *o, const char *k, const char *v)
{
    if (!strcmp(k, "cd_music"))
        o->cd_music = truthy(v);
    else if (!strcmp(k, "cd_image"))
        snprintf(o->cd_image, sizeof o->cd_image, "%s", v);
    else if (!strcmp(k, "game_dir"))
        snprintf(o->game_dir, sizeof o->game_dir, "%s", v);
    else if (!strcmp(k, "ist_args"))
        o->ist_args = atoi(v);
    else if (!strcmp(k, "source"))
        snprintf(o->source, sizeof o->source, "%s", v);
    else if (!strcmp(k, "edition"))
        snprintf(o->edition, sizeof o->edition, "%s", v);
    else if (!strcmp(k, "base_dir"))
        snprintf(o->base_dir, sizeof o->base_dir, "%s", v);
    else if (!strcmp(k, "video"))
        snprintf(o->video, sizeof o->video, "%s", v);
    else
        return -1;
    return 0;
}

int options_load_ini(LaleOptions *o, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    char line[1200];
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        n++;
        char *p = trim(line);
        if (!*p || *p == ';' || *p == '#' || *p == '[')
            continue;
        char *eq = strchr(p, '=');
        if (!eq) {
            fprintf(stderr, "%s:%d: expected key = value\n", path, n);
            continue;
        }
        *eq = 0;
        char *k = trim(p), *v = trim(eq + 1);
        if (set_key(o, k, v) != 0)
            fprintf(stderr, "%s:%d: unknown key '%s'\n", path, n, k);
    }
    fclose(f);
    return 0;
}

int options_parse_args(LaleOptions *o, int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--cd-music"))
            o->cd_music = 1;
        else if (!strcmp(a, "--no-cd-music"))
            o->cd_music = 0;
        else if (!strcmp(a, "--cd-image") && i + 1 < argc)
            snprintf(o->cd_image, sizeof o->cd_image, "%s", argv[++i]);
        else if (!strcmp(a, "--cd-play") && i + 1 < argc)
            o->cd_play_track = atoi(argv[++i]);
        else if (!strcmp(a, "--seconds") && i + 1 < argc)
            o->cd_play_seconds = atoi(argv[++i]);
        else if (!strcmp(a, "--game-dir") && i + 1 < argc)
            snprintf(o->game_dir, sizeof o->game_dir, "%s", argv[++i]);
        else if (!strcmp(a, "--ist-args") && i + 1 < argc)
            o->ist_args = atoi(argv[++i]);
        else if (!strcmp(a, "--source") && i + 1 < argc)
            snprintf(o->source, sizeof o->source, "%s", argv[++i]);
        else if (!strcmp(a, "--edition") && i + 1 < argc)
            snprintf(o->edition, sizeof o->edition, "%s", argv[++i]);
        else if (!strcmp(a, "--base-dir") && i + 1 < argc)
            snprintf(o->base_dir, sizeof o->base_dir, "%s", argv[++i]);
        else if (!strcmp(a, "--video") && i + 1 < argc)
            snprintf(o->video, sizeof o->video, "%s", argv[++i]);
        else if (a[0] != '-' && !o->source[0])          /* a file or folder dropped on the program */
            snprintf(o->source, sizeof o->source, "%s", a);
        else if (!strcmp(a, "--frames") && i + 1 < argc)
            o->frames = atoi(argv[++i]);
        else if (!strcmp(a, "--screenshot") && i + 1 < argc)
            snprintf(o->screenshot, sizeof o->screenshot, "%s", argv[++i]);
        else {
            fprintf(stderr, "unknown or incomplete option: %s\n", a);
            return -1;
        }
    }
    return 0;
}
