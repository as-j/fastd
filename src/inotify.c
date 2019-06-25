/*
  Copyright (c) 2012-2016, Matthias Schiffer <mschiffer@universe-factory.net>
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/**
   \file

   inotifyhronous notifications
*/


#include "inotify.h"
#include "fastd.h"
#include "config.h"

#include <sys/inotify.h>

/** Initializes the inotify notification sockets */
void fastd_inotify_init(void) {
	int fd = inotify_init1(IN_CLOEXEC|IN_NONBLOCK);
	if (fd == -1)
		exit_errno("inotify");

	ctx.inotify_fd = FASTD_POLL_FD(POLL_TYPE_INOTFY, fd);

	fastd_poll_fd_register(&ctx.inotify_fd);
}

/** Reads and handles a single notification from the inotify notification socket */
void fastd_inotify_handle(void) {
	/* Loop while events can be read from inotify file descriptor. */
	for (;;) {
		/* Read some events. */
		/* Some systems cannot read integer variables if they are not
		   properly aligned. On other systems, incorrect alignment may
		   decrease performance. Hence, the buffer used for reading from
		   the inotify file descriptor should have the same alignment as
		   struct inotify_event. */
		char buf[4096]
			__attribute__ ((aligned(__alignof__(struct inotify_event))));
		const struct inotify_event *event;
		ssize_t len = read(ctx.inotify_fd.fd, buf, sizeof(buf));
		if (len == -1 && errno != EAGAIN) 
            exit_errno("inotify failed read");

		/* If the nonblocking read() found no events to read, then
		   it returns -1 with errno set to EAGAIN. In that case,
		   we exit the loop. */

		if (len <= 0)
			break;

		/* Loop over all events in the buffer */
		char *ptr;
		for (ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {
			event = (const struct inotify_event *) ptr;

			/* Print event type */
			const char *type = "unknown: ";
			if (event->mask & IN_DELETE)
				type = "IN_DELETE: ";
			if (event->mask & IN_CLOSE_WRITE)
				type = "IN_CLOSE_WRITE: ";
			if (event->mask & IN_MODIFY)
				type = "IN_MODIFY: ";
			if (event->mask & IN_MOVED_TO) 
				type = "IN_MOVED_TO: ";

			/* Print the name of the watched directory */
			const char *dir = NULL;
			int i;
			for (i = 0; i < VECTOR_LEN(ctx.inotify_wd); i++) {
				fastd_inotify_watch_descriptor_t *watch_descriptor = VECTOR_INDEX(ctx.inotify_wd, i);
				if (watch_descriptor->wd == event->wd) {
					dir = watch_descriptor->dir;
					break;
				}
			}
			if (!dir) {
				pr_warn("Failed to find dir for watch descriptor: %i", event->wd);
				continue;
			}

			if (event->len == 0)
				continue;

			const char *name = event->name;
			if (name[strlen(name)-1] == '~') {
				pr_verbose("ignoring file `%s' as it seems to be a backup file", name);
				continue;
			}

			if (event->mask & IN_DELETE) {
				fastd_config_delete_peer(dir, name);
			}
			else {
				pr_info("Doing reload due to: %s for peer: %s in dir: %s", type, name, dir);
				fastd_config_load_peer(dir, name);
			}
		}
	}
}

/**
 Watch a directory for config changes, don't add it again if already watching it
*/
void fastd_inotify_add_config_dir(const char *dir) {

	int i;
	for (i = 0; i < VECTOR_LEN(ctx.inotify_wd); i++) {
		fastd_inotify_watch_descriptor_t *watch_descriptor = VECTOR_INDEX(ctx.inotify_wd, i);
		if (!strcmp(watch_descriptor->dir, dir)) {
			return;
		}
	}

	int wd = inotify_add_watch(ctx.inotify_fd.fd, dir,
			IN_CLOSE_WRITE|
			IN_MODIFY|
			IN_DELETE|
			IN_MOVED_TO|
			IN_ONLYDIR);
	if( wd == -1) {
		pr_error_errno("inotify_add_watch");
		return;
	}
	pr_verbose("Added watcher for %s wd: %i", dir, wd);
	fastd_inotify_watch_descriptor_t *watch_descriptor = fastd_new0(fastd_inotify_watch_descriptor_t);
	watch_descriptor->dir = fastd_strdup(dir);
	watch_descriptor->wd = wd;
	VECTOR_ADD(ctx.inotify_wd, watch_descriptor);
}

