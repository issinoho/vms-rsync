/* vms_crtl_init.c - C RTL feature switches for rsync, set before main() through
   LIB$INITIALIZE (vms-rsync, D5). A switch whose DECC$ logical name the site or
   user has defined is left alone, so any of them can be overridden. These are
   the switches the Phase 0 probes ran with (NOTES N1). */
#include <stdlib.h>
#include <unixlib.h>

static const struct { const char *name; int value; } switches[] = {
    { "DECC$ARGV_PARSE_STYLE", 1 },          /* argument case kept under PARSE_STYLE=EXTENDED */
    { "DECC$EFS_CHARSET", 1 },               /* ODS-5 names */
    { "DECC$EFS_CASE_PRESERVE", 1 },
    { "DECC$FILENAME_UNIX_REPORT", 1 },      /* names reported in Unix form */
    { "DECC$FILENAME_UNIX_NO_VERSION", 1 },  /* and without versions */
    { "DECC$READDIR_DROPDOTNOTYPE", 1 },     /* "README", not "README." */
    { "DECC$UNIX_PATH_BEFORE_LOGNAME", 1 },  /* a path before a same-named logical name */
    { "DECC$EFS_FILE_TIMESTAMPS", 1 },       /* st_mtime is the modification date (N1 probe 5) */
    { "DECC$POSIX_SEEK_STREAM_FILE", 1 },
    { "DECC$FILE_SHARING", 1 },              /* others may read files rsync has open */
};

/* The program's name, for getprogname() (compats.c, HAVE___PROGNAME). It is
   "rsync", the name a local --server is started by (vms_spawn.c). */
char *__progname = "rsync";

static void vms_crtl_init(void)
{
    size_t i;
    for (i = 0; i < sizeof switches / sizeof switches[0]; i++) {
        int index;
        if (getenv(switches[i].name) != NULL) continue;
        index = decc$feature_get_index(switches[i].name);
        if (index >= 0) decc$feature_set_value(index, 1, switches[i].value);
    }
}

/* The LIB$INITIALIZE table entry: a 32-bit pointer in a psect with exactly
   these attributes, and a reference so the linker keeps LIB$INITIALIZE. */
#pragma nostandard
#pragma extern_model save
#pragma extern_model strict_refdef "LIB$INITIALIZE" nopic, con, rel, gbl, noshr, noexe, nowrt, novec, long
#if __INITIAL_POINTER_SIZE
# pragma __pointer_size __save
# pragma __pointer_size 32
#else
# pragma __required_pointer_size __save
# pragma __required_pointer_size 32
#endif
void (* const vms_crtl_init_table[])(void) = { vms_crtl_init };
#if __INITIAL_POINTER_SIZE
# pragma __pointer_size __restore
#else
# pragma __required_pointer_size __restore
#endif
int LIB$INITIALIZE(void);
#pragma extern_model strict_refdef
int vms_crtl_init_ref = (int) LIB$INITIALIZE;
#pragma extern_model restore
#pragma standard
