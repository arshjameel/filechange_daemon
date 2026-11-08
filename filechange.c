#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>

#include <sys/inotify.h>
#include <libnotify/notify.h>

#define EXT_SUCCESS 0
#define EXT_ERR_TOO_FEW_ARGS 1
#define EXT_ERR_INIT_INOTIFY 2
#define EXT_ERR_ADD_WATCH 3
#define EXT_ERR_BASE_PATH_NULL 4
#define EXT_ERR_READ_INOTIFY 5
#define EXT_ERR_INIT_LIBNOTIFY 6

#define REWATCH_TRIES 5

int IeventQueue = -1;
int IeventStatus = -1;

char *ProgramTitle = "filechange";

void signal_handler(int signal) {
  int closeStatus = -1;
  printf("\nSignal received, cleaning up...\n");
  
  closeStatus = inotify_rm_watch(IeventQueue, IeventStatus);
  if (closeStatus == -1) {
    fprintf(stderr, "Error removing from watch queue!\n");
  }

  close(IeventQueue);
  exit(EXIT_SUCCESS);
}

int main(int argc, char** argv) {

  bool libnotifyInitStatus = false;

  // -- POINTERS --
  char *basePath = NULL;
  char *token = NULL;
  char *notificationMsg = NULL;

  NotifyNotification *notifyHandle;

  char buffer[4096];
  int readLength;

  const struct inotify_event* watchEvent;

  const uint32_t watchMask = IN_CREATE | IN_DELETE | IN_ACCESS | IN_CLOSE_WRITE | IN_MODIFY | IN_MOVED_TO | IN_MOVE_SELF;

  if (argc < 2) {
    fprintf(stderr, "USAGE: filechange PATH\n");
    exit(EXT_ERR_TOO_FEW_ARGS);
  }

  basePath = (char *)malloc(sizeof(char)*(strlen(argv[1]) + 1));
  strcpy(basePath, argv[1]);

  token = strtok(basePath, "/");
  while (token != NULL) {
    basePath = token;
    token = strtok(NULL, "/");
  }

  //printf("%s\n", basePath);

  if (basePath == NULL) {
    fprintf(stderr, "Error getting base path.\n");
    exit(EXT_ERR_BASE_PATH_NULL);
  }

  libnotifyInitStatus = notify_init(ProgramTitle);
  if (!libnotifyInitStatus) {
    fprintf(stderr, "Error initialising libnotify.\n");
    exit(EXT_ERR_INIT_LIBNOTIFY);
  }

  IeventQueue = inotify_init();
  if (IeventQueue == -1) {
    fprintf(stderr, "Error initialising inotify instance.\n");
    exit(EXT_ERR_INIT_INOTIFY);
  }

  IeventStatus = inotify_add_watch(IeventQueue, argv[1], watchMask);
  if (IeventStatus == -1) {
    fprintf(stderr, "Error adding file to watch instance.\n");
    exit(EXT_ERR_ADD_WATCH);
  }

  signal(SIGABRT, signal_handler);
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  while (true) {
    printf("Waiting for ievent...\n");

    readLength = read(IeventQueue, buffer, sizeof(buffer));
    if (readLength == -1) {
      fprintf(stderr, "Error reading from inotify instance.\n");
      exit(EXT_ERR_READ_INOTIFY);
    }

    for (char *buffPtr = buffer; buffPtr < buffer + readLength;
        buffPtr += sizeof(struct inotify_event) + watchEvent->len) {

      notificationMsg = NULL;
      watchEvent = (const struct inotify_event *) buffPtr;

      /* event type.  */
      if (watchEvent->mask & IN_CREATE)       notificationMsg   =   "File created.\n";
      if (watchEvent->mask & IN_DELETE)       notificationMsg   =   "File deleted.\n";
      if (watchEvent->mask & IN_ACCESS)       notificationMsg   =   "File accessed.\n";
      if (watchEvent->mask & IN_MODIFY)       notificationMsg   =   "File written/modified.\n";
      if (watchEvent->mask & IN_CLOSE_WRITE)  notificationMsg   =   "File written and closed.\n";
      if (watchEvent->mask & IN_MOVED_TO)     notificationMsg   =   "File renamed.\n";
      if (watchEvent->mask & IN_MOVE_SELF)    notificationMsg   =   "File moved.\n";

      if (watchEvent->mask & IN_IGNORED) {
        // some text editors move the file when modified, changing its inode.
        // watch the new inode after the old one is deleted.
        for (int tries = 0; tries < REWATCH_TRIES; tries++) {
          IeventStatus = inotify_add_watch(IeventQueue, argv[1], watchMask);
          if (IeventStatus != -1) break;
          usleep(10000);  // in case the new file takes some time to be created
        }
        if (IeventStatus == -1) {
          fprintf(stderr, "File gone, could not re-add watch.\n");
          exit(EXT_ERR_ADD_WATCH);
        }
        notificationMsg = "File replaced.\n";
      }

      if (notificationMsg == NULL) continue;

      // Print the name of the watched directory.
      //printf("%s\n", notificationMsg);
      
      notifyHandle = notify_notification_new(basePath, notificationMsg, "dialog-information");
      if (notifyHandle == NULL) {
        fprintf(stderr, "notification handle was null!\n");
        continue;
      }

      notify_notification_show(notifyHandle, NULL);
      g_object_unref(G_OBJECT(notifyHandle)); // release GObject reference created by notifyHandle
    } 
  }

  exit(EXT_SUCCESS);
}
