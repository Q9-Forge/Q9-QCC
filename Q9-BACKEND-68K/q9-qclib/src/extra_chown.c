/* chown(3) -- change file ownership via the RBF file descriptor sector. */
#include <rbf.h>
#include <types.h>
#include <modes.h>

/* owner is group.user packed into one int: high byte group, low byte user
   (same order as ql68's own -gu=<group>.<user> option). Only FD_OWN is
   touched; FD_ATT/FD_DAT are read back unmodified so they are not
   accidentally cleared by the SetStt write-back. */
int qf_chown(char *path, int owner)
{
    path_id p;
    fd_stats fd;
    error_code err;

    err = _os_open(path, FAM_READ | FAM_WRITE, &p);
    if (err != 0)
        return 1;
    err = _os_gs_fd(p, sizeof(fd_stats), &fd);
    if (err == 0) {
        fd.fd_own_group = (owner >> 8) & 0xff;
        fd.fd_own_user = owner & 0xff;
        err = _os_ss_fd(p, &fd);
    }
    _os_close(p);
    return err != 0 ? 1 : 0;
}
