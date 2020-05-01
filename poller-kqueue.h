#include <sys/event.h>

static int
poller_create(void)
{
	int pfd = kqueue();
	if (pfd < 0) {
		err(1, "kqueue create");
	}
	return pfd;
}

static void
poller_add_read(int pfd, int rfd, enum poller_mode mode)
{
	struct kevent ev;
	ev.ident = rfd;
	ev.filter = EVFILT_READ;
	ev.fflags = 0;
	ev.data = 0;
	ev.udata = 0;

	switch (mode) {
	case POLLER_EDGE:
		ev.flags = EV_ADD | EV_CLEAR;
		break;
	case POLLER_LEVEL:
		ev.flags = EV_ADD;
		break;
	case POLLER_ONESHOT:
		ev.flags = EV_ADD | EV_ONESHOT;
		break;
	}

	if (kevent(pfd, &ev, 1, NULL, 0, NULL) < 0) {
		err(1, "kevent add");
	}
}

static void
poller_rearm_read(int pfd, int rfd)
{
	poller_add_read(pfd, rfd, POLLER_ONESHOT);
}

static int
poller_wait(int pfd, intptr_t *avail, int ms)
{
	struct timespec ts, *tsp = NULL;
	if (ms >= 0) {
		ts.tv_sec = ms / 1000;
		ts.tv_nsec = (ms % 1000) * 1000000;
		tsp = &ts;
	}

	struct kevent ev;
	int nev = kevent(pfd, NULL, 0, &ev, 1, tsp);
	if (nev < 0) {
		err(1, "kevent wait");
	}
	if (nev == 0) {
		return -1;
	}
	if (avail != NULL) {
		*avail = (ssize_t)ev.data;
	}
	return (int)ev.ident;
}

