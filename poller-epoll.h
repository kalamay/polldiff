#include <sys/epoll.h>
#include <sys/ioctl.h>

static int
poller_create(void)
{
	int pfd = epoll_create1(EPOLL_CLOEXEC);
	if (pfd < 0) {
		err(1, "epoll create");
	}
	return pfd;
}

static void
poller_add_read(int pfd, int rfd, enum poller_mode mode)
{
	struct epoll_event ev;
	ev.data.fd = rfd;

	switch (mode) {
	case POLLER_EDGE:
		ev.events = EPOLLIN | EPOLLET;
		break;
	case POLLER_LEVEL:
		ev.events = EPOLLIN;
		break;
	case POLLER_ONESHOT:
		ev.events = EPOLLIN | EPOLLONESHOT;
		break;
	}

	if (epoll_ctl(pfd, EPOLL_CTL_ADD, rfd, &ev) < 0) {
		err(1, "epoll add");
	}
}

static void
poller_rearm_read(int pfd, int rfd)
{
	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLONESHOT;
	ev.data.fd = rfd;

	if (epoll_ctl(pfd, EPOLL_CTL_MOD, rfd, &ev) < 0) {
		err(1, "epoll mod");
	}
}

static int
poller_wait(int pfd, intptr_t *avail, int ms)
{
	struct epoll_event ev;
	int nev = epoll_wait(pfd, &ev, 1, ms);
	if (nev < 0) {
		err(1, "epoll wait");
	}
	if (nev == 0) {
		return -1;
	}
	if (avail != NULL) {
		int nread = 0;
		ioctl(ev.data.fd, FIONREAD, &nread);
		*avail = (intptr_t)nread;
	}
	return ev.data.fd;
}

