#ifndef _PATHS_H
#define _PATHS_H 1

/* Standard search paths */
#define _PATH_DEFPATH    "/usr/bin:/bin"
#define _PATH_STDPATH    "/usr/bin:/bin:/usr/sbin:/sbin"

/* Standard shell and system utilities */
#define _PATH_BSHELL     "/bin/sh"
#define _PATH_CSHELL     "/bin/csh"
#define _PATH_SHELLS     "/etc/shells"

/* Standard system directories */
#define _PATH_VARRUN     "/var/run/"
#define _PATH_VARTMP     "/var/tmp/"
#define _PATH_TMP        "/tmp/"

/* Devices */
#define _PATH_DEV        "/dev/"
#define _PATH_DEVNULL    "/dev/null"
#define _PATH_DEVZERO    "/dev/zero"
#define _PATH_TTY        "/dev/tty"
#define _PATH_CONSOLE    "/dev/console"
#define _PATH_KLOG       "/dev/klog"

/* System files and databases */
#define _PATH_ETC        "/etc"
#define _PATH_PASSWD     "/etc/passwd"
#define _PATH_GROUP      "/etc/group"
#define _PATH_SHADOW     "/etc/shadow"
#define _PATH_HOSTS      "/etc/hosts"
#define _PATH_SERVICES   "/etc/services"
#define _PATH_PROTOCOLS  "/etc/protocols"
#define _PATH_NETWORKS   "/etc/networks"

/* Logs and run status */
#define _PATH_LASTLOG    "/var/log/lastlog"
#define _PATH_UTMP       "/var/run/utmp"
#define _PATH_WTMP       "/var/log/wtmp"
#define _PATH_MAN        "/usr/share/man"

/* Mail / Spool */
#define _PATH_MAILDIR    "/var/mail"
#define _PATH_MOUNTED    "/etc/mtab"
#define _PATH_MNTTAB     "/etc/fstab"

#endif /* _PATHS_H */