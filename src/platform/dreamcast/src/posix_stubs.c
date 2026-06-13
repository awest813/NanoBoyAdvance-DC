#include <errno.h>

struct utimbuf;

int truncate(const char *path, long length) { errno = ENOSYS; return -1; }
int fchmodat(int fd, const char *path, int mode, int flag) { errno = ENOSYS; return -1; }
long pathconf(const char *path, int name) { errno = ENOSYS; return -1; }
int fchmod(int fd, int mode) { errno = ENOSYS; return -1; }
int _rename_r(void *reent, const char *old, const char *new_name) { errno = ENOSYS; return -1; }
int utime(const char *path, const struct utimbuf *times) { (void)path; (void)times; return 0; }
