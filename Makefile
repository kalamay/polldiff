CFLAGS:= -Wall -Wextra -Werror

run: bin/poller
	./$<

bin/%: %.c poller-kqueue.h poller-epoll.h mu.h | bin
	$(CC) $(CFLAGS) $< -o $@

bin:
	mkdir -p bin

clean:
	rm -f bin/poller

.PHONY: run clean
