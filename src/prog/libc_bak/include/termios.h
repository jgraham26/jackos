#ifndef _TERMIOS_H
#define _TERMIOS_H 1

#include <sys/types.h>
#include <sys/ioctl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Type definitions */
typedef unsigned char  cc_t;
typedef unsigned int   speed_t;
typedef unsigned int   tcflag_t;

#define NCCS 32

struct termios {
    tcflag_t c_iflag;      /* Input mode flags */
    tcflag_t c_oflag;      /* Output mode flags */
    tcflag_t c_cflag;      /* Control mode flags */
    tcflag_t c_lflag;      /* Local mode flags */
    cc_t     c_line;       /* Line discipline */
    cc_t     c_cc[NCCS];   /* Control characters */
    speed_t  c_ispeed;     /* Input baud rate */
    speed_t  c_ospeed;     /* Output baud rate */
};

/* --------------------------------------------------------------------------
 * c_cc Subscript Names (indices in c_cc array)
 * -------------------------------------------------------------------------- */
#define VINTR    0   /* Interrupt character (Ctrl-C) */
#define VQUIT    1   /* Quit character (Ctrl-\) */
#define VERASE   2   /* Erase character (Backspace) */
#define VKILL    3   /* Kill line character (Ctrl-U) */
#define VEOF     4   /* End-of-file character (Ctrl-D) */
#define VTIME    5   /* Non-canonical read timeout */
#define VMIN     6   /* Non-canonical min bytes to read */
#define VSWTC    7
#define VSTART   8   /* Start character (Ctrl-Q) */
#define VSTOP    9   /* Stop character (Ctrl-S) */
#define VSUSP    10  /* Suspend character (Ctrl-Z) */
#define VEOL     11  /* Additional end-of-line */
#define VREPRINT 12  /* Reprint unread characters (Ctrl-R) */
#define VDISCARD 13
#define VWERASE  14  /* Word erase character (Ctrl-W) */
#define VLNEXT   15  /* Literal next character (Ctrl-V) */
#define VEOL2    16

/* --------------------------------------------------------------------------
 * c_iflag: Input Modes
 * -------------------------------------------------------------------------- */
#define IGNBRK  0000001
#define BRKINT  0000002
#define IGNPAR  0000004
#define PARMRK  0000010
#define INPCK   0000020
#define ISTRIP  0000040
#define INLCR   0000100
#define IGNCR   0000200
#define ICRNL   0000400
#define IUCLC   0001000
#define IXON    0002000
#define IXANY   0004000
#define IXOFF   0010000
#define IMAXBEL 0020000
#define IUTF8   0040000

/* --------------------------------------------------------------------------
 * c_oflag: Output Modes
 * -------------------------------------------------------------------------- */
#define OPOST   0000001
#define OLCUC   0000002
#define ONLCR   0000004
#define OCRNL   0000010
#define ONOCR   0000020
#define ONLRET  0000040
#define OFILL   0000100
#define OFDEL   0000200

/* --------------------------------------------------------------------------
 * c_cflag: Control Modes & Baud Rates
 * -------------------------------------------------------------------------- */
#define CSIZE   0000060
#define   CS5   0000000
#define   CS6   0000020
#define   CS7   0000040
#define   CS8   0000060
#define CSTOPB  0000100
#define CREAD   0000200
#define PARENB  0000400
#define PARODD  0001000
#define HUPCL   0002000
#define CLOCAL  0004000

/* Baud rate values (matching standard Linux/glibc) */
#define B0      0000000
#define B50     0000001
#define B75     0000002
#define B110    0000003
#define B134    0000004
#define B150    0000005
#define B200    0000006
#define B300    0000007
#define B600    0000010
#define B1200   0000011
#define B1800   0000012
#define B2400   0000013
#define B4800   0000014
#define B9600   0000015
#define B19200  0000016
#define B38400  0000017
#define B57600  0010001
#define B115200 0010002
#define B230400 0010003
#define CBAUD   0010017

/* --------------------------------------------------------------------------
 * c_lflag: Local Modes
 * -------------------------------------------------------------------------- */
#define ISIG    0000001  /* Enable signals (INTR, QUIT, SUSP) */
#define ICANON  0000002  /* Canonical input (line-buffered) */
#define XCASE   0000004
#define ECHO    0000010  /* Echo input characters */
#define ECHOE   0000020  /* Visually erase characters */
#define ECHOK   0000040  /* Echo newline after kill */
#define ECHONL  0000100  /* Echo newline even if ECHO is off */
#define NOFLSH  0000200  /* Disable buffer flush on signal */
#define TOSTOP  0000400  /* Send SIGTTOU on background output */
#define ECHOCTL 0001000  /* Echo control chars as ^X */
#define ECHOPRT 0002000
#define ECHOKE  0004000
#define FLUSHO  0010000
#define PENDIN  0040000
#define IEXTEN  0100000  /* Enable implementation-defined input processing */

/* --------------------------------------------------------------------------
 * tcsetattr actions & tcflush queue selectors
 * -------------------------------------------------------------------------- */
#define TCSANOW   0  /* Make changes immediately */
#define TCSADRAIN 1  /* Make changes after transmitted output is drained */
#define TCSAFLUSH 2  /* Make changes after draining output and discarding input */

#define TCIFLUSH  0  /* Flush unread input */
#define TCOFLUSH  1  /* Flush unsent output */
#define TCIOFLUSH 2  /* Flush both */

#define TCOOFF    0  /* Suspend output */
#define TCOON     1  /* Restart output */
#define TCIOFF    2  /* Transmit STOP character */
#define TCION     3  /* Transmit START character */

/* Standard function declarations */
int     tcgetattr(int fd, struct termios *termios_p);
int     tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
int     tcsendbreak(int fd, int duration);
int     tcdrain(int fd);
int     tcflush(int fd, int queue_selector);
int     tcflow(int fd, int action);

speed_t cfgetispeed(const struct termios *termios_p);
speed_t cfgetospeed(const struct termios *termios_p);
int     cfsetispeed(struct termios *termios_p, speed_t speed);
int     cfsetospeed(struct termios *termios_p, speed_t speed);
int     cfsetspeed(struct termios *termios_p, speed_t speed);
void    cfmakeraw(struct termios *termios_p);

pid_t   tcgetsid(int fd);

#ifdef __cplusplus
}
#endif

#endif /* _TERMIOS_H */