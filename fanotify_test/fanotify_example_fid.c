#define _GNU_SOURCE
       #include <errno.h>
       #include <fcntl.h>
       #include <limits.h>
       #include <stdio.h>
       #include <stdlib.h>
       #include <sys/types.h>
       #include <sys/stat.h>
       #include <sys/fanotify.h>
       #include <unistd.h>

       #define BUF_SIZE 256

       int
       main(int argc, char *argv[])
       {
           int fd, ret, event_fd, mount_fd;
           ssize_t len, path_len;
           char path[PATH_MAX];
           char procfd_path[PATH_MAX];
           char events_buf[BUF_SIZE];
           struct file_handle *file_handle;
           struct fanotify_event_metadata *metadata;
           struct fanotify_event_info_fid *fid;
           const char *file_name;
           struct stat sb;

           if (argc != 2) {
               fprintf(stderr, "Invalid number of command line arguments.\n");
               exit(EXIT_FAILURE);
           }

           mount_fd = open(argv[1], O_RDONLY);
           if (mount_fd == -1) {
               perror(argv[1]);
               exit(EXIT_FAILURE);
           }

           /* Create an fanotify file descriptor with FAN_REPORT_DFID_NAME as
              a flag so that program can receive fid events with directory
              entry name. */

           fd = fanotify_init(FAN_REPORT_FID, O_RDONLY | O_LARGEFILE);
           if (fd == -1) {
               perror("fanotify_init");
               exit(EXIT_FAILURE);
           }

           /* Place a mark on the filesystem object supplied in argv[1]. */

           ret = fanotify_mark(fd, FAN_MARK_ADD,
                               FAN_MOVE,
                               AT_FDCWD, argv[1]);
           if (ret == -1) {
               perror("fanotify_mark");
               exit(EXIT_FAILURE);
           }

           printf("Listening for events.\n");

           /* Read events from the event queue into a buffer. */

           len = read(fd, events_buf, sizeof(events_buf));
           if (len == -1 && errno != EAGAIN) {
               perror("read");
               exit(EXIT_FAILURE);
           }

           /* Process all events within the buffer. */

           for (metadata = (struct fanotify_event_metadata *) events_buf;
                   FAN_EVENT_OK(metadata, len);
                   metadata = FAN_EVENT_NEXT(metadata, len)) {
               fid = (struct fanotify_event_info_fid *) (metadata + 1);
               file_handle = (struct file_handle *) fid->handle;

               /* Ensure that the event info is of the correct type. */

               if (fid->hdr.info_type == FAN_EVENT_INFO_TYPE_FID ||
                   fid->hdr.info_type == FAN_EVENT_INFO_TYPE_DFID) {
                   file_name = NULL;
               }  else {
                   fprintf(stderr, "Received unexpected event info type.\n");
                   exit(EXIT_FAILURE);
               }

               if (metadata->mask == FAN_MOVE)
                   printf("FAN_MOVE (file moved):\n");


            /* metadata->fd is set to FAN_NOFD when the group identifies
               objects by file handles.  To obtain a file descriptor for
               the file object corresponding to an event you can use the
               struct file_handle that's provided within the
               fanotify_event_info_fid in conjunction with the
               open_by_handle_at(2) system call.  A check for ESTALE is
               done to accommodate for the situation where the file handle
               for the object was deleted prior to this system call. */

               event_fd = open_by_handle_at(mount_fd, file_handle, O_RDONLY);
               if (event_fd == -1) {
                   if (errno == ESTALE) {
                       printf("File handle is no longer valid. "
                               "File has been deleted\n");
                       continue;
                   } else {
                       perror("open_by_handle_at");
                       exit(EXIT_FAILURE);
                   }
               }

               snprintf(procfd_path, sizeof(procfd_path), "/proc/self/fd/%d",
                       event_fd);

               /* Retrieve and print the path of the modified dentry. */

               path_len = readlink(procfd_path, path, sizeof(path) - 1);
               if (path_len == -1) {
                   perror("readlink");
                   exit(EXIT_FAILURE);
               }

               path[path_len] = '\0';
               printf("\tDirectory '%s' has been modified.\n", path);

               if (file_name) {
                   ret = fstatat(event_fd, file_name, &sb, 0);
                   if (ret == -1) {
                       if (errno != ENOENT) {
                           perror("fstatat");
                           exit(EXIT_FAILURE);
                       }
                       printf("\tEntry '%s' does not exist.\n", file_name);
                   } else if ((sb.st_mode & S_IFMT) == S_IFDIR) {
                       printf("\tEntry '%s' is a subdirectory.\n", file_name);
                   } else {
                       printf("\tEntry '%s' is not a subdirectory.\n",
                               file_name);
                   }
               }

               /* Close associated file descriptor for this event. */

               close(event_fd);
           }

           printf("All events processed successfully. Program exiting.\n");
           exit(EXIT_SUCCESS);
       }
