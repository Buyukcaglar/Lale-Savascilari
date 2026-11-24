/*
 * install.c - see install.h.
 * Verified (docs/INSTALL.md has the runs): the 2005 release from its ZIP,
 * from 7Z and RAR copies of it and from a folder; the CD from the redump
 * layout image, from the redumper dump folder (offset shift repaired: the
 * same LALE.CHD, SHA-1 for SHA-1), from an ISO and from an existing CHD.
 */
#include "install.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "platform/cdrom.h"
#include "platform/cdsector.h"
#include "platform/chd.h"
#include "platform/drive.h"
#include "platform/iso9660.h"
#include "platform/zipfile.h"
#include "platform/uitext.h"

#define CHD_NAME "LALE.CHD"
#define TMP_NAME "_import"          /* <base>/DATA/_import: unpacked archives, removed afterwards */

typedef struct Ui {
    InstallProgressFn fn;
    void             *ctx;
    const char       *text;
    uint64_t          done, total;
} Ui;

static int ui_tick(Ui *ui)
{
    return ui->fn ? ui->fn(ui->ctx, ui->text, ui->done, ui->total) : 0;
}

static void seterr(char *err, size_t n, const char *msg, const char *arg)
{
    if (err && n)
        snprintf(err, n, "%s%s%s", msg, arg ? ": " : "", arg ? arg : "");
}

/* ---- names and folders --------------------------------------------------------- */

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
            return 0;
    return *a == *b;
}

static int ext_is(const char *name, const char *ext)
{
    const char *dot = strrchr(name, '.');
    return dot && ieq(dot + 1, ext);
}

static int is_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int is_file(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static uint64_t file_size(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 ? (uint64_t)st.st_size : 0;
}

int install_mkdir(const char *path)
{
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0777);
#endif
    return is_dir(path) ? 0 : -1;
}

/* The entry of `dir` called `name`, whatever its case: its path in out. */
static int find_name(const char *dir, const char *name, char *out, size_t size)
{
    DIR *d = opendir(dir);
    struct dirent *de;
    int found = 0;
    if (!d)
        return 0;
    while (!found && (de = readdir(d)) != NULL)
        if (ieq(de->d_name, name)) {
            snprintf(out, size, "%s/%s", dir, de->d_name);
            found = 1;
        }
    closedir(d);
    return found;
}

static const char *const required[] = {
    "0GFL.CKD", "3D.CKD", "FRPOS.CKD", "IKON.CKD", "IVIR.CKD", "MUZIK.CKD", "SAVAS.CKD", "SOUNDS.CKD",
    "TRDOS.DAT", "GRPHAZIR.DAT",
};

int install_have_files(const char *dir)
{
    char path[1200];
    for (size_t i = 0; i < sizeof required / sizeof required[0]; i++)
        if (!find_name(dir, required[i], path, sizeof path) || !is_file(path))
            return 0;
    return 1;
}

int install_have_chd(const char *data_dir, char *path, size_t size)
{
    char p[1200];
    if (!find_name(data_dir, CHD_NAME, p, sizeof p) || !is_file(p))
        return 0;
    if (path)
        snprintf(path, size, "%s", p);
    return 1;
}

/* Where a file of the game belongs: 1 DATA, 2 SAVE, 0 not needed (the DOS
   programs, their PIFs and texts). */
static int wanted(const char *name)
{
    static const char *const data[] = { "TRDOS.DAT", "GRPHAZIR.DAT", "BOSCH.KOR", "SONPAL.PAL" };
    static const char *const save[] = { "GRP.DAT", "SES.CFG" };
    if (ext_is(name, "CKD") || ext_is(name, "DSD"))
        return 1;
    if (ext_is(name, "SAV"))
        return 2;
    for (size_t i = 0; i < sizeof data / sizeof data[0]; i++)
        if (ieq(name, data[i]))
            return 1;
    for (size_t i = 0; i < sizeof save / sizeof save[0]; i++)
        if (ieq(name, save[i]))
            return 2;
    return 0;
}

/* The folder at or below root (depth levels down) that holds 3D.CKD. */
static int find_game_folder(const char *root, int depth, char *out, size_t size)
{
    char path[1200];
    DIR *d;
    struct dirent *de;
    int found = 0;
    if (find_name(root, "3D.CKD", path, sizeof path) && is_file(path)) {
        snprintf(out, size, "%s", root);
        return 1;
    }
    if (depth <= 0 || !(d = opendir(root)))
        return 0;
    while (!found && (de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.')
            continue;
        snprintf(path, sizeof path, "%s/%s", root, de->d_name);
        if (is_dir(path))
            found = find_game_folder(path, depth - 1, out, size);
    }
    closedir(d);
    return found;
}

static int image_rank(const char *name)
{
    return ext_is(name, "cue") ? 3 : ext_is(name, "chd") ? 2 : ext_is(name, "iso") ? 1 : 0;
}

/* The disc image at or below root: a cue sheet before a CHD before an ISO. */
static int find_image(const char *root, int depth, char *out, size_t size)
{
    char path[1200];
    DIR *d = opendir(root);
    struct dirent *de;
    int best = 0;
    if (!d)
        return 0;
    while ((de = readdir(d)) != NULL) {
        int r = image_rank(de->d_name);
        if (r <= best)
            continue;
        snprintf(path, sizeof path, "%s/%s", root, de->d_name);
        if (is_file(path)) {
            best = r;
            snprintf(out, size, "%s", path);
        }
    }
    if (!best && depth > 0) {
        rewinddir(d);
        while (!best && (de = readdir(d)) != NULL) {
            if (de->d_name[0] == '.')
                continue;
            snprintf(path, sizeof path, "%s/%s", root, de->d_name);
            if (is_dir(path))
                best = find_image(path, depth - 1, out, size);
        }
    }
    closedir(d);
    return best != 0;
}

/* Removes the unpacking folder and everything in it (that folder only). */
static void remove_tree(const char *path)
{
    char sub[1400];
    DIR *d;
    struct dirent *de;
    if (!strstr(path, TMP_NAME) || !(d = opendir(path)))
        return;
    while ((de = readdir(d)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        snprintf(sub, sizeof sub, "%s/%s", path, de->d_name);
        if (is_dir(sub))
            remove_tree(sub);
        else
            remove(sub);
    }
    closedir(d);
    rmdir(path);
}

/* ---- files -> DATA and SAVE ------------------------------------------------------- */

static int copy_file(const char *src, const char *dst, Ui *ui, char *err, size_t errlen)
{
    static uint8_t buf[1 << 18];
    char tmp[1300];
    FILE *in = fopen(src, "rb"), *out = NULL;
    size_t n;
    int rc = -1;
    snprintf(tmp, sizeof tmp, "%s.tmp", dst);
    if (!in) {
        seterr(err, errlen, T_E_READ, src);
        return -1;
    }
    out = fopen(tmp, "wb");
    if (!out) {
        seterr(err, errlen, T_E_CREATE, dst);
        fclose(in);
        return -1;
    }
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            seterr(err, errlen, T_E_WRITE_FULL, dst);
            goto done;
        }
        ui->done += n;
        if (ui_tick(ui)) {
            rc = 0;                             /* cancelled */
            goto done;
        }
    }
    if (ferror(in)) {
        seterr(err, errlen, T_E_READ, src);
        goto done;
    }
    rc = 1;
done:
    fclose(in);
    if (fclose(out) != 0 && rc == 1) {
        seterr(err, errlen, T_E_WRITE_FULL, dst);
        rc = -1;
    }
    if (rc == 1) {
        remove(dst);
        if (rename(tmp, dst) != 0) {
            seterr(err, errlen, T_E_CREATE, dst);
            rc = -1;
        }
    }
    if (rc != 1)
        remove(tmp);
    return rc;
}

/* The game files of `dir` into <base>/DATA and <base>/SAVE; move = they are
   our own unpacked copies.  Returns INSTALL_FILES, 0 cancelled, -1 error. */
static int import_files(const char *dir, const char *base, int move, Ui *ui, char *err, size_t errlen)
{
    char src[1300], dst[1300], name[300];
    DIR *d = opendir(dir);
    struct dirent *de;
    int rc = INSTALL_FILES;
    if (!d) {
        seterr(err, errlen, T_E_READ_FOLDER, dir);
        return -1;
    }
    ui->text = T_P_COPY_FILES;
    ui->done = ui->total = 0;
    while ((de = readdir(d)) != NULL)
        if (wanted(de->d_name)) {
            snprintf(src, sizeof src, "%s/%s", dir, de->d_name);
            ui->total += file_size(src);
        }
    rewinddir(d);
    while (rc == INSTALL_FILES && (de = readdir(d)) != NULL) {
        int where = wanted(de->d_name);
        snprintf(src, sizeof src, "%s/%s", dir, de->d_name);
        if (!where || !is_file(src))
            continue;
        snprintf(name, sizeof name, "%s", de->d_name);
        for (char *c = name; *c; c++)
            *c = (char)toupper((unsigned char)*c);
        snprintf(dst, sizeof dst, "%s/%s/%s", base, where == 1 ? "DATA" : "SAVE", name);
        if (where == 2 && is_file(dst)) {       /* the player's own: never replaced */
            ui->done += file_size(src);
            continue;
        }
        if (move) {
            remove(dst);
            if (rename(src, dst) == 0) {
                ui->done += file_size(dst);
                if (ui_tick(ui))
                    rc = 0;
                continue;
            }
        }
        int r = copy_file(src, dst, ui, err, errlen);
        if (r != 1)
            rc = r;
    }
    closedir(d);
    if (rc == INSTALL_FILES) {
        snprintf(dst, sizeof dst, "%s/DATA", base);
        if (!install_have_files(dst)) {
            seterr(err, errlen, T_E_INCOMPLETE, dir);
            rc = -1;
        }
    }
    return rc;
}

/* ---- a disc -> DATA/LALE.CHD ------------------------------------------------------- */

typedef struct Counted {                /* a source that counts what it cannot read */
    const CdromSource *src;
    uint32_t volume, bad_volume, bad_other;
} Counted;

static int counted_read(void *ctx, uint32_t lba, uint8_t *raw)
{
    Counted *c = ctx;
    if (c->src->read_sector(c->src->ctx, lba, raw) == 0)
        return 0;
    if (lba < c->volume)
        c->bad_volume++;
    else
        c->bad_other++;
    return -1;
}

static int iso_from_source(void *ctx, uint32_t lba, uint8_t *sector)
{
    const CdromSource *s = ctx;
    uint8_t raw[2352];
    if (s->read_sector(s->ctx, lba, raw) != 0)
        return -1;
    memcpy(sector, raw + (raw[15] == 2 ? 24 : 16), 2048);
    return 0;
}

static int chd_progress(void *ctx, uint32_t done, uint32_t total)
{
    Ui *ui = ctx;
    ui->done = done;
    ui->total = total;
    return ui_tick(ui);
}

/* 0 when the disc behind src is the game's: an ISO 9660 volume with 3D.CKD. */
static int check_game_disc(const CdromSource *src, uint32_t *volume, char *err, size_t errlen)
{
    IsoFs fs;
    IsoEntry e;
    if (iso_mount(&fs, iso_from_source, (void *)src) != 0 || iso_find(&fs, "3D.CKD", &e) != 0) {
        seterr(err, errlen, T_E_NOT_GAME_CD, NULL);
        return -1;
    }
    *volume = fs.volume_sectors;
    return 0;
}

static int write_chd(const char *base, const CdromTrack *tracks, int ntracks, uint32_t leadout,
                     const CdromSource *src, Ui *ui, char *err, size_t errlen)
{
    char tmp[1300], final[1300], num[160];
    Counted cnt = { src, 0, 0, 0 };
    CdromSource csrc = { counted_read, NULL, &cnt };
    ChdStats st;
    Chd *c;
    int rc;
    if (check_game_disc(src, &cnt.volume, err, errlen))
        return -1;
    snprintf(tmp, sizeof tmp, "%s/DATA/" CHD_NAME ".tmp", base);
    snprintf(final, sizeof final, "%s/DATA/" CHD_NAME, base);
    ui->text = T_P_WRITE_CHD;
    rc = chd_create_cd(tmp, tracks, ntracks, leadout, &csrc, chd_progress, ui, &st, err, errlen);
    if (rc != 0)
        return rc == 1 ? 0 : -1;
    if (cnt.bad_volume) {
        snprintf(num, sizeof num, T_E_BAD_SECTORS_FMT,
                 (unsigned)cnt.bad_volume);
        seterr(err, errlen, num, NULL);
        remove(tmp);
        return -1;
    }
    if (cnt.bad_other)
        fprintf(stderr, "import: %u sectors outside the files could not be read; written as silence\n",
                (unsigned)cnt.bad_other);
    ui->text = T_P_CHECK_DATA_CHD;
    c = chd_open(tmp, err, errlen);
    rc = c ? chd_verify(c, chd_progress, ui, err, errlen) : -1;
    chd_close(c);
    if (rc != 0) {
        remove(tmp);
        return rc == 1 ? 0 : -1;
    }
    remove(final);
    if (rename(tmp, final) != 0) {
        seterr(err, errlen, T_E_CREATE, final);
        remove(tmp);
        return -1;
    }
    return INSTALL_DISC;
}

/* -- cue/bin, with redumper's offset shift put right (install.h) -- */

typedef struct Shift {
    uint32_t first;             /* sectors from here on are read `bytes` later */
    uint32_t data_end;          /* end of the data track: up to here they are scrambled */
    uint32_t leadout;
    uint32_t bytes;
} Shift;

static int cue_read(void *ctx, uint32_t lba, uint8_t *raw)
{
    (void)ctx;
    return cdrom_read_raw(lba, raw);
}

static int shift_read(void *ctx, uint32_t lba, uint8_t *raw)
{
    const Shift *s = ctx;
    uint8_t buf[2 * 2352];
    if (lba < s->first)
        return cdrom_read_raw(lba, raw);
    if (cdrom_read_raw(lba, buf) != 0)
        return -1;
    if (lba + 1 >= s->leadout || cdrom_read_raw(lba + 1, buf + 2352) != 0)
        memset(buf + 2352, 0, 2352);            /* past the image: not in the dump */
    memcpy(raw, buf + s->bytes, 2352);
    if (lba < s->data_end)
        cd_scramble(raw);
    return 0;
}

static int bcd(uint32_t v) { return (int)(v / 10 * 16 + v % 10); }

static int good_mode1(const uint8_t *raw, uint32_t lba)
{
    uint32_t a = lba + CDROM_LEADIN;
    return memcmp(raw, cd_sync, 12) == 0 && raw[12] == bcd(a / 4500) && raw[13] == bcd(a / 75 % 60)
           && raw[14] == bcd(a % 75) && raw[15] == 1 && cd_ecc_ok(raw);
}

/* 1 when the image carries the shift; s filled in. */
static int detect_shift(const CdromTrack *t, int ntracks, uint32_t leadout, Shift *s)
{
    uint8_t buf[2 * 2352], raw[2352];
    uint32_t end, first;
    if (ntracks < 2 || t[0].audio || !t[1].audio || t[0].sector_size != 2352 || t[0].start != 0
        || t[0].sectors < 400)
        return 0;
    end = t[1].start;
    for (first = end; first > end - 300; first--)       /* the unsynced tail of the data track */
        if (cdrom_read_raw(first - 1, raw) != 0 || memcmp(raw, cd_sync, 12) == 0)
            break;
    if (first == end || first == end - 300)
        return 0;
    if (cdrom_read_raw(first, buf) != 0 || cdrom_read_raw(first + 1, buf + 2352) != 0)
        return 0;
    s->first = first;
    s->data_end = end;
    s->leadout = leadout;
    for (s->bytes = 4; s->bytes < 2352; s->bytes += 4) {
        if (memcmp(buf + s->bytes, cd_sync, 12) != 0)
            continue;
        for (uint32_t lba = first; lba < end; lba++)    /* every one of them must come out right */
            if (shift_read(s, lba, raw) != 0 || !good_mode1(raw, lba))
                return 0;
        return 1;
    }
    return 0;
}

static int import_cue(const char *cue, const char *base, Ui *ui, char *err, size_t errlen)
{
    CdromTrack tracks[CDROM_MAX_TRACKS];
    Shift shift;
    int n = 0, rc;
    if (cdrom_open(cue) != 0) {
        seterr(err, errlen, cdrom_error(), cue);
        return -1;
    }
    for (int i = 1; i <= CDROM_MAX_TRACKS; i++)
        if (cdrom_track(i))
            tracks[n++] = *cdrom_track(i);
    CdromSource src = { cue_read, NULL, NULL };
    if (detect_shift(tracks, n, cdrom_leadout(), &shift)) {
        fprintf(stderr, "import: offset shift of %u samples after sector %u (a redumper split): read at the "
                "shifted position from there\n", (unsigned)(shift.bytes / 4), (unsigned)shift.first);
        src.read_sector = shift_read;
        src.ctx = &shift;
    }
    rc = write_chd(base, tracks, n, cdrom_leadout(), &src, ui, err, errlen);
    cdrom_close();
    return rc;
}

/* -- .iso: the data track's user data -- */

typedef struct IsoFile {
    FILE    *f;
    uint32_t sectors;
} IsoFile;

static int isofile_read(void *ctx, uint32_t lba, uint8_t *raw)
{
    IsoFile *i = ctx;
    memset(raw, 0, 2352);
    raw[15] = 1;
    if (lba >= i->sectors || fseek(i->f, (long)lba * 2048, SEEK_SET) != 0
        || fread(raw + 16, 1, 2048, i->f) != 2048)
        return -1;
    return 0;
}

static int import_iso(const char *path, const char *base, Ui *ui, char *err, size_t errlen)
{
    IsoFile iso = { fopen(path, "rb"), (uint32_t)(file_size(path) / 2048) };
    CdromTrack t;
    CdromSource src = { isofile_read, NULL, &iso };
    int rc;
    if (!iso.f || iso.sectors < 20 || iso.sectors > 450000) {
        seterr(err, errlen, T_E_READ_IMAGE, path);
        if (iso.f)
            fclose(iso.f);
        return -1;
    }
    memset(&t, 0, sizeof t);
    t.number = 1;
    t.sectors = iso.sectors;
    t.sector_size = 2048;
    rc = write_chd(base, &t, 1, iso.sectors, &src, ui, err, errlen);
    fclose(iso.f);
    return rc;
}

/* -- a CHD someone else made: checked, then copied -- */

static int chdsrc_read(void *ctx, uint32_t lba, uint8_t *raw) { return chd_read_sector(ctx, lba, raw); }

static int import_chd(const char *path, const char *base, Ui *ui, char *err, size_t errlen)
{
    char final[1300];
    uint32_t volume;
    Chd *c = chd_open(path, err, errlen);
    int rc;
    if (!c)
        return -1;
    CdromSource src = { chdsrc_read, NULL, c };
    rc = check_game_disc(&src, &volume, err, errlen);
    if (rc == 0) {
        ui->text = T_P_CHECK_CHD;
        rc = chd_verify(c, chd_progress, ui, err, errlen);
        if (rc == 1) {
            chd_close(c);
            return 0;
        }
    }
    chd_close(c);
    if (rc != 0)
        return -1;
    snprintf(final, sizeof final, "%s/DATA/" CHD_NAME, base);
    if (ieq(path, final))
        return INSTALL_DISC;                    /* it is the installed one */
    ui->text = T_P_COPY_CHD;
    ui->done = 0;
    ui->total = file_size(path);
    rc = copy_file(path, final, ui, err, errlen);
    return rc == 1 ? INSTALL_DISC : rc;
}

static int import_drive(const char *drive, const char *base, Ui *ui, char *err, size_t errlen)
{
    CdromTrack tracks[CDROM_MAX_TRACKS];
    CdromSource src;
    uint32_t leadout;
    int n = drive_open(drive, tracks, &leadout, &src, err, errlen), rc;
    if (n < 0)
        return -1;
    rc = write_chd(base, tracks, n, leadout, &src, ui, err, errlen);
    src.close(src.ctx);
    return rc;
}

static int import_image(const char *path, const char *base, Ui *ui, char *err, size_t errlen)
{
    if (ext_is(path, "cue"))
        return import_cue(path, base, ui, err, errlen);
    if (ext_is(path, "chd"))
        return import_chd(path, base, ui, err, errlen);
    return import_iso(path, base, ui, err, errlen);
}

/* A folder: a disc image in it wins over loose files (the disc has the music). */
static int import_tree(const char *dir, const char *base, int move, Ui *ui, char *err, size_t errlen)
{
    char found[1200];
    if (find_image(dir, 3, found, sizeof found))
        return import_image(found, base, ui, err, errlen);
    if (find_game_folder(dir, 4, found, sizeof found))
        return import_files(found, base, move, ui, err, errlen);
    seterr(err, errlen, T_E_NOTHING_HERE, dir);
    return -1;
}

/* ---- archives ------------------------------------------------------------------------ */

static int unpack_ext(const char *name)
{
    static const char *const ext[] = { "CKD", "DAT", "KOR", "PAL", "DSD", "CFG", "SAV", "CUE", "BIN", "ISO", "CHD" };
    for (size_t i = 0; i < sizeof ext / sizeof ext[0]; i++)
        if (ext_is(name, ext[i]))
            return 1;
    return 0;
}

static int zip_tick(void *ctx, uint32_t bytes)
{
    Ui *ui = ctx;
    uint64_t before = ui->done;
    ui->done += bytes;
    int stop = ui_tick(ui);
    ui->done = before;
    return stop;
}

/* The game's files and disc images of a ZIP, all into the one folder tmp.
   Returns 1, 0 cancelled, -1 when this reader cannot do it. */
static int unzip(const char *path, const char *tmp, Ui *ui, char *err, size_t errlen)
{
    char dst[1400];
    ZipFile *z = zip_open(path, err, errlen);
    int rc = 1;
    if (!z)
        return -1;
    ui->text = T_P_UNPACK;
    ui->done = ui->total = 0;
    for (int i = 0; i < zip_count(z); i++)
        if (unpack_ext(zip_entry(z, i)->name))
            ui->total += zip_entry(z, i)->usize;
    for (int i = 0; rc == 1 && i < zip_count(z); i++) {
        const ZipEntry *e = zip_entry(z, i);
        const char *leaf = strrchr(e->name, '/');
        leaf = leaf ? leaf + 1 : e->name;
        if (!*leaf || !unpack_ext(leaf) || strchr(leaf, ':'))
            continue;
        snprintf(dst, sizeof dst, "%s/%s", tmp, leaf);
        if (is_file(dst))
            continue;                           /* the same name in two folders: the first */
        int r = zip_extract(z, e, dst, zip_tick, ui, err, errlen);
        if (r != 0)
            rc = r == 1 ? 0 : -1;
        ui->done += e->usize;
    }
    zip_close(z);
    return rc;
}

#ifdef _WIN32
static int run_tool(const char *exe, const char *args, Ui *ui)
{
    char path[1024], cmd[4096];
    wchar_t wcmd[4096];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD code = 1;
    if (!ExpandEnvironmentStringsA(exe, path, sizeof path) || GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
        return -1;
    snprintf(cmd, sizeof cmd, "\"%s\" %s", path, args);
    if (!MultiByteToWideChar(CP_UTF8, 0, cmd, -1, wcmd, 4096))
        return -1;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessW(NULL, wcmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return -1;
    while (WaitForSingleObject(pi.hProcess, 100) == WAIT_TIMEOUT)
        if (ui_tick(ui)) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return 1;
        }
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0 ? 0 : -1;
}

/* 0 unpacked, 1 cancelled, -1 no tool could */
static int unpack_with_tools(const char *archive, const char *tmp, Ui *ui)
{
    static const char *const tools[][2] = {
        { "%SystemRoot%\\System32\\tar.exe",        "-xf \"%s\" -C \"%s\"" },    /* bsdtar, Windows 10+ */
        { "%ProgramFiles%\\7-Zip\\7z.exe",          "x -y \"%s\" -o\"%s\"" },
        { "%ProgramFiles(x86)%\\7-Zip\\7z.exe",     "x -y \"%s\" -o\"%s\"" },
    };
    char args[3000];
    for (size_t i = 0; i < sizeof tools / sizeof tools[0]; i++) {
        snprintf(args, sizeof args, tools[i][1], archive, tmp);
        int r = run_tool(tools[i][0], args, ui);
        if (r >= 0)
            return r;
        remove_tree(tmp);
        install_mkdir(tmp);
    }
    return -1;
}
#else
static int run_tool(char *const argv[], Ui *ui)
{
    int status = 0;
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    while (waitpid(pid, &status, WNOHANG) == 0) {
        usleep(100000);
        if (ui_tick(ui)) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            return 1;
        }
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static int unpack_with_tools(const char *archive, const char *tmp, Ui *ui)
{
    char out7[1300];
    snprintf(out7, sizeof out7, "-o%s", tmp);
    char *a = (char *)archive, *t = (char *)tmp;
    char *const tools[][7] = {
        { "/usr/bin/bsdtar", "-xf", a, "-C", t, NULL },     /* macOS: libarchive, reads 7z and rar */
        { "bsdtar", "-xf", a, "-C", t, NULL },
        { "7z", "x", "-y", a, out7, NULL },
        { "7zz", "x", "-y", a, out7, NULL },
        { "unar", "-f", "-o", t, a, NULL },
    };
    for (size_t i = 0; i < sizeof tools / sizeof tools[0]; i++) {
        int r = run_tool(tools[i], ui);
        if (r >= 0)
            return r;
        remove_tree(tmp);
        install_mkdir(tmp);
    }
    return -1;
}
#endif

static int import_archive(const char *path, const char *base, Ui *ui, char *err, size_t errlen)
{
    char tmp[1300];
    int rc = -1;
    snprintf(tmp, sizeof tmp, "%s/DATA/" TMP_NAME, base);
    remove_tree(tmp);
    if (install_mkdir(tmp) != 0) {
        seterr(err, errlen, T_E_CREATE, tmp);
        return -1;
    }
    if (ext_is(path, "zip"))
        rc = unzip(path, tmp, ui, err, errlen);
    if (rc < 0) {                               /* not a ZIP, or one for a full unpacker */
        remove_tree(tmp);
        install_mkdir(tmp);
        ui->text = T_P_UNPACK;
        ui->done = ui->total = 0;
        int r = unpack_with_tools(path, tmp, ui);
        rc = r == 0 ? 1 : r == 1 ? 0 : -1;
        if (rc < 0)
            seterr(err, errlen, T_E_UNPACK, path);
    }
    if (rc == 1)
        rc = import_tree(tmp, base, 1, ui, err, errlen);
    remove_tree(tmp);
    return rc;
}

/* ---- the front door --------------------------------------------------------------------- */

int install_import(const char *base, const char *source, InstallProgressFn progress, void *pctx,
                   char *err, size_t errlen)
{
    char dir[1300], src[1200];
    Ui ui = { progress, pctx, "", 0, 0 };
    size_t n;
    if (err && errlen)
        err[0] = 0;
    snprintf(dir, sizeof dir, "%s/DATA", base);
    if (install_mkdir(dir) != 0) {
        seterr(err, errlen, T_E_CREATE_FOLDER, dir);
        return -1;
    }
    snprintf(dir, sizeof dir, "%s/SAVE", base);
    if (install_mkdir(dir) != 0) {
        seterr(err, errlen, T_E_CREATE_FOLDER, dir);
        return -1;
    }
    if (drive_is_optical(source))
        return import_drive(source, base, &ui, err, errlen);
    snprintf(src, sizeof src, "%s", source);
    for (n = strlen(src); n > 1 && (src[n - 1] == '/' || src[n - 1] == '\\') && src[n - 2] != ':'; n--)
        src[n - 1] = 0;
    if (is_dir(src))
        return import_tree(src, base, 0, &ui, err, errlen);
    if (!is_file(src)) {
        seterr(err, errlen, T_E_NO_SUCH, source);
        return -1;
    }
    if (image_rank(src))
        return import_image(src, base, &ui, err, errlen);
    if (ext_is(src, "bin") || ext_is(src, "img")) {     /* a track file: its cue sheet */
        char *dot = strrchr(src, '.');
        memcpy(dot, ".cue", 5);
        if (is_file(src))
            return import_cue(src, base, &ui, err, errlen);
        seterr(err, errlen, T_E_BIN_NEEDS_CUE, source);
        return -1;
    }
    return import_archive(src, base, &ui, err, errlen);
}

static int add(char paths[][1024], int n, int max, const char *path)
{
    if (n < max)
        snprintf(paths[n++], 1024, "%s", path);
    return n;
}

int install_find_sources(const char *dir, char paths[][1024], int max)
{
    char path[1200], found[1200];
    DIR *d = opendir(dir);
    struct dirent *de;
    int n = 0;
    if (!d)
        return 0;
    /* 0 disc images here, 1 in the subfolders, 2 the files here, 3 in the subfolders, 4 archives */
    for (int pass = 0; pass < 5; pass++) {
        if (pass == 2 && find_name(dir, "3D.CKD", path, sizeof path) && is_file(path))
            n = add(paths, n, max, dir);
        if (pass == 2)
            continue;
        rewinddir(d);
        while ((de = readdir(d)) != NULL) {
            const char *nm = de->d_name;
            if (nm[0] == '.' || ieq(nm, "DATA") || ieq(nm, "SAVE") || ieq(nm, "VIDEO"))
                continue;
            snprintf(path, sizeof path, "%s/%s", dir, nm);
            if (pass == 0 && image_rank(nm) && is_file(path))
                n = add(paths, n, max, path);
            else if (pass == 1 && is_dir(path) && find_image(path, 0, found, sizeof found))
                n = add(paths, n, max, found);
            else if (pass == 3 && is_dir(path) && find_game_folder(path, 0, found, sizeof found))
                n = add(paths, n, max, found);
            else if (pass == 4 && (ext_is(nm, "zip") || ext_is(nm, "7z") || ext_is(nm, "rar")) && is_file(path))
                n = add(paths, n, max, path);
        }
    }
    closedir(d);
    return n;
}

void install_default_sescfg(const char *data_dir, const char *save_dir)
{
    /* docs/FORMATS.md "SES.CFG": driver name, the SDI configuration (u32
       fields), the memory-mode word */
    static const struct { uint8_t off; uint32_t v; } field[] = {
        { 0x00, 6 }, { 0x04, 0xCD }, { 0x08, 0x220 }, { 0x0C, 5 }, { 0x14, 7 },
        { 0x30, 1 }, { 0x34, 3 }, { 0x38, 1 }, { 0x3C, 3 }, { 0x48, 2 },
    };
    uint8_t cfg[107] = "SDISB16.DSD";
    char path[1300];
    FILE *f;
    if (find_name(save_dir, "SES.CFG", path, sizeof path) || find_name(data_dir, "SES.CFG", path, sizeof path))
        return;
    for (size_t i = 0; i < sizeof field / sizeof field[0]; i++)
        for (int b = 0; b < 4; b++)
            cfg[13 + field[i].off + b] = (uint8_t)(field[i].v >> (8 * b));
    cfg[105] = 2;
    snprintf(path, sizeof path, "%s/SES.CFG", save_dir);
    f = fopen(path, "wb");
    if (f) {
        fwrite(cfg, 1, sizeof cfg, f);
        fclose(f);
    }
}
