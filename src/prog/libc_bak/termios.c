#include <termios.h>
#include <sys/ioctl.h>
#include <errno.h>

int tcgetattr(int fd, struct termios *termios_p) {
    return ioctl(fd, TCGETS, termios_p);
}

int tcsetattr(int fd, int optional_actions, const struct termios *termios_p) {
    unsigned long cmd;
    switch (optional_actions) {
        case TCSANOW:   cmd = TCSETS;  break;
        case TCSADRAIN: cmd = TCSETSW; break;
        case TCSAFLUSH: cmd = TCSETSF; break;
        default:
            errno = EINVAL;
            return -1;
    }
    return ioctl(fd, cmd, termios_p);
}

speed_t cfgetispeed(const struct termios *termios_p) {
    return termios_p->c_ispeed;
}

speed_t cfgetospeed(const struct termios *termios_p) {
    return termios_p->c_ospeed;
}

int cfsetispeed(struct termios *termios_p, speed_t speed) {
    termios_p->c_ispeed = speed;
    return 0;
}

int cfsetospeed(struct termios *termios_p, speed_t speed) {
    termios_p->c_ospeed = speed;
    return 0;
}

int cfsetspeed(struct termios *termios_p, speed_t speed) {
    termios_p->c_ispeed = speed;
    termios_p->c_ospeed = speed;
    return 0;
}

void cfmakeraw(struct termios *t) {
    t->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    t->c_oflag &= ~OPOST;
    t->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t->c_cflag &= ~(CSIZE | PARENB);
    t->c_cflag |= CS8;
    t->c_cc[VMIN] = 1;
    t->c_cc[VTIME] = 0;
}

int tcflush(int fd, int queue_selector) {
    return ioctl(fd, TCFLSH, queue_selector);
}

int tcdrain(int fd) {
    return ioctl(fd, TCSBRK, 1);
}

int tcflow(int fd, int action) {
    return ioctl(fd, TCXONC, action);
}

int tcsendbreak(int fd, int duration) {
    return ioctl(fd, TCSBRK, duration);
}