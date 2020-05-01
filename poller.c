#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include "mu.h"

enum poller_mode {
	POLLER_EDGE,
	POLLER_LEVEL,
	POLLER_ONESHOT
};

enum poller_filter {
	POLLER_IN    = 1,
	POLLER_OUT   = 2,
	POLLER_INOUT = POLLER_IN | POLLER_OUT,
};

#if __APPLE__ || __FreeBSD__ || __NetBSD__ || __OpenBSD__ || __DragonFly__
# define HAVE_KQUEUE 1
# include "poller-kqueue.h"
#endif
#if __linux__
# define HAVE_EPOLL 1
# include "poller-epoll.h"
#endif

static int fds[2];
static int pfd;

static void
unblock(int fd)
{
	int flags;
	mu_assert_call(flags = fcntl(fd, F_GETFL, 0));
	mu_assert_call(fcntl(fd, F_SETFL, flags | O_NONBLOCK));
}

static void
teardown(void)
{
	close(fds[0]);
	close(fds[1]);
	close(pfd);
}

static void
setup_read(enum poller_mode mode)
{
	mu_teardown = teardown;

	mu_assert_call(pipe(fds));
	unblock(fds[0]);
	unblock(fds[1]);
	pfd = poller_create();
	poller_add_read(pfd, fds[0], mode);
}

static void
test_edge_read_none(void)
{
	setup_read(POLLER_EDGE);

	intptr_t nread;
	int fd;

	// Write to the pipe and the event should fire.
	mu_assert_call(write(fds[1], "abcdefgh", 8));
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 8);

	// Nothing furthur is writen so we should get a timeout.
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, -1);

	// Write more and the event should fire again.
	mu_assert_call(write(fds[1], "ijklmnop", 8));
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 16);
}

static void
test_edge_read_some(void)
{
	setup_read(POLLER_EDGE);

	intptr_t nread;
	int fd;
	char buf[32] = {0};

	// Write to the pipe and the event should fire.
	mu_assert_call(write(fds[1], "abcdefgh", 8));
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 8);

	// Read only half of the available data.
	mu_assert_int_eq(read(fd, buf, 4), 4);
	mu_assert_str_eq(buf, "abcd");

	// FIXME: The event fires conditionally based on the poller.
	// Using epoll, the event does not fire as no "new" data has arrived on the
	// pipe. With kqueue, the partial read causes the event to be rescheduled.
	fd = poller_wait(pfd, &nread, 10);
#if HAVE_EPOLL
	mu_assert_int_eq(fd, -1);
#elif HAVE_KQUEUE
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 4);
#endif

	// Write more and the event should always fire again.
	mu_assert_call(write(fds[1], "ijklmnop", 8));
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 12);
	mu_assert_int_eq(read(fd, buf, 12), 12);
	mu_assert_str_eq(buf, "efghijklmnop");
}

static void
test_edge_read_all(void)
{
	setup_read(POLLER_EDGE);

	intptr_t nread;
	int fd;
	char buf[32] = {0};

	// Write to the pipe and the event should fire.
	mu_assert_call(write(fds[1], "abcdefgh", 8));
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 8);

	// Read all the available data.
	mu_assert_int_eq(read(fd, buf, 8), 8);
	mu_assert_str_eq(buf, "abcdefgh");

	// Nothing furthur is writen so we should get a timeout.
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, -1);

	// Write more and the event should fire again.
	mu_assert_call(write(fds[1], "ijklmnop", 8));
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 8);

	mu_assert_int_eq(read(fd, buf, 8), 8);
	mu_assert_str_eq(buf, "ijklmnop");
}

static void
test_edge_read_more(void)
{
	setup_read(POLLER_EDGE);

	intptr_t nread;
	int fd;
	char buf[32] = {0};

	// Write to the pipe and the event should fire.
	mu_assert_call(write(fds[1], "abcdefgh", 8));
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 8);

	// Write more before reading.
	mu_assert_call(write(fds[1], "ijklmnop", 8));

	// Read the known available data.
	mu_assert_int_eq(read(fd, buf, 8), 8);
	mu_assert_str_eq(buf, "abcdefgh");

	// The even should immediately fire again.
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 8);

	mu_assert_int_eq(read(fd, buf, 8), 8);
	mu_assert_str_eq(buf, "ijklmnop");
}

static void
test_level_read_none(void)
{
	setup_read(POLLER_LEVEL);

	intptr_t nread;
	int fd;

	// Write to the pipe and the event should fire.
	mu_assert_call(write(fds[1], "abcdefgh", 8));
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 8);

	// Nothing furthur is writen but we should still get an event.
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 8);

	// Write more and the event should still fire.
	mu_assert_call(write(fds[1], "ijklmnop", 8));
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 16);
}

static void
test_level_read_some(void)
{
	setup_read(POLLER_LEVEL);

	intptr_t nread;
	int fd;
	char buf[32] = {0};

	// Write to the pipe and the event should fire.
	mu_assert_call(write(fds[1], "abcdefgh", 8));
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 8);

	// Read only half of the available data.
	mu_assert_int_eq(read(fd, buf, 4), 4);
	mu_assert_str_eq(buf, "abcd");

	// The event should still fire.
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);

	// Write more and the event should always fire again.
	mu_assert_call(write(fds[1], "ijklmnop", 8));
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 12);
	mu_assert_int_eq(read(fd, buf, 12), 12);
	mu_assert_str_eq(buf, "efghijklmnop");
}

static void
test_level_read_all(void)
{
	setup_read(POLLER_LEVEL);

	intptr_t nread;
	int fd;
	char buf[32] = {0};

	// Write to the pipe and the event should fire.
	mu_assert_call(write(fds[1], "abcdefgh", 8));
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 8);

	// Read all the available data.
	mu_assert_int_eq(read(fd, buf, 8), 8);
	mu_assert_str_eq(buf, "abcdefgh");

	// Nothing furthur is writen so we should get a timeout.
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, -1);

	// Write more and the event should fire again.
	mu_assert_call(write(fds[1], "ijklmnop", 8));
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 8);

	mu_assert_int_eq(read(fd, buf, 8), 8);
	mu_assert_str_eq(buf, "ijklmnop");
}

static void
test_level_read_more(void)
{
	setup_read(POLLER_LEVEL);

	intptr_t nread;
	int fd;
	char buf[32] = {0};

	// Write to the pipe and the event should fire.
	mu_assert_call(write(fds[1], "abcdefgh", 8));
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 8);

	// Write more before reading.
	mu_assert_call(write(fds[1], "ijklmnop", 8));

	// Read the known available data.
	mu_assert_int_eq(read(fd, buf, 8), 8);
	mu_assert_str_eq(buf, "abcdefgh");

	// The even should immediately fire again.
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 8);

	mu_assert_int_eq(read(fd, buf, 8), 8);
	mu_assert_str_eq(buf, "ijklmnop");
}

static void
test_once_read_none(void)
{
	setup_read(POLLER_ONESHOT);

	intptr_t nread;
	int fd;

	// Write to the pipe and the event should fire.
	mu_assert_call(write(fds[1], "abcdefgh", 8));
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 8);

	// Nothing furthur is writen so we should get a timeout.
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, -1);

	// Write more and the event still shouldn't fire.
	mu_assert_call(write(fds[1], "ijklmnop", 8));
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, -1);

	poller_rearm_read(pfd, fds[0]);

	// Now the event should fire.
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 16);
}

static void
test_once_read_some(void)
{
	setup_read(POLLER_ONESHOT);

	intptr_t nread;
	int fd;
	char buf[32] = {0};

	// Write to the pipe and the event should fire.
	mu_assert_call(write(fds[1], "abcdefgh", 8));
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 8);

	// Read only half of the available data.
	mu_assert_int_eq(read(fd, buf, 4), 4);
	mu_assert_str_eq(buf, "abcd");

	// The event should no longer fire.
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, -1);

	// Now the event should fire.
	poller_rearm_read(pfd, fds[0]);
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);

	// This should fire too.
	poller_rearm_read(pfd, fds[0]);
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);

	// Write more and the event should still fire.
	poller_rearm_read(pfd, fds[0]);
	mu_assert_call(write(fds[1], "ijklmnop", 8));
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 12);
	mu_assert_int_eq(read(fd, buf, 12), 12);
	mu_assert_str_eq(buf, "efghijklmnop");
}

static void
test_once_read_all(void)
{
	setup_read(POLLER_ONESHOT);

	intptr_t nread;
	int fd;
	char buf[32] = {0};

	// Write to the pipe and the event should fire.
	mu_assert_call(write(fds[1], "abcdefgh", 8));
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 8);

	// Read all the available data.
	mu_assert_int_eq(read(fd, buf, 8), 8);
	mu_assert_str_eq(buf, "abcdefgh");

	// Nothing furthur is writen so we should get a timeout.
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, -1);

	// Write more and but the event still shouldn't fire.
	mu_assert_call(write(fds[1], "ijklmnop", 8));
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, -1);

	poller_rearm_read(pfd, fds[0]);

	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 8);

	mu_assert_int_eq(read(fd, buf, 8), 8);
	mu_assert_str_eq(buf, "ijklmnop");
}

static void
test_once_read_more(void)
{
	setup_read(POLLER_ONESHOT);

	intptr_t nread;
	int fd;
	char buf[32] = {0};

	// Write to the pipe and the event should fire.
	mu_assert_call(write(fds[1], "abcdefgh", 8));
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 8);

	// Write more before reading.
	mu_assert_call(write(fds[1], "ijklmnop", 8));

	// Read the known available data.
	mu_assert_int_eq(read(fd, buf, 8), 8);
	mu_assert_str_eq(buf, "abcdefgh");

	// The even should have been disabled.
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, -1);

	poller_rearm_read(pfd, fds[0]);

	// The even should immediately fire again.
	fd = poller_wait(pfd, &nread, 10);
	mu_assert_int_eq(fd, fds[0]);
	mu_assert_int_eq(nread, 8);

	mu_assert_int_eq(read(fd, buf, 8), 8);
	mu_assert_str_eq(buf, "ijklmnop");
}

int
main(void)
{
	mu_init("poll");
	mu_run(test_edge_read_none);
	mu_run(test_edge_read_some);
	mu_run(test_edge_read_all);
	mu_run(test_edge_read_more);
	mu_run(test_level_read_none);
	mu_run(test_level_read_some);
	mu_run(test_level_read_all);
	mu_run(test_level_read_more);
	mu_run(test_once_read_none);
	mu_run(test_once_read_some);
	mu_run(test_once_read_all);
	mu_run(test_once_read_more);

	return 0;
}

