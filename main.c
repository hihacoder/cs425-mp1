#include <getopt.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int num_processes = 4;
int num_snapshots = 5;
int seed = 100;
int*** channels;

// Returns a random int in {0,...,n-1}
int randn(int n) {
  return (int)(((double)rand()) / RAND_MAX * n);
}

// Returns a random process id, excluding the passed in id
int random_process(int id) {
  int result = randn(num_processes - 1);
  if (result == id) {
    result = num_processes - 1;
  }
  return result;
}

void parse_flags(int argc, char** argv) {
  while (1) {
    static struct option long_opts[] = {
      {"num_processes", required_argument, 0, 'p'},
      {"num_snapshots", required_argument, 0, 's'},
      {"seed", required_argument, 0, 'r'},
      {0, 0, 0, 0}
    };

    int option_index = 0;
    int c = getopt_long(argc, argv, "p:s:r:", long_opts, &option_index);
    if (c == -1) {
      break;
    }

    switch (c) {
      case 'p':
        num_processes = atoi(optarg);
        break;
      case 's':
        num_snapshots = atoi(optarg);
        break;
      case 'r':
        seed = atoi(optarg);
        break;
    }
  }
}

typedef struct {
  int id;
  int money;
} process_t;

void process_init(process_t* p, int id) {
  p->id = id;
  p->money = 100;
}

void process_handle_message(process_t* p, int fd) {
  char type;
  if (read(fd, &type, 1) != 1) {
    perror("read error");
    return;
  }
  unsigned char amt;
  switch (type) {
    case 0x1:
      read(fd, &amt, sizeof(amt));
      p->money += amt;
      break;
    default:
      fprintf(stderr, "Undefined message type\n");
  }
}

void process_send_message(process_t* state, int to) {
  unsigned char amt = randn(256);  // random amount
  char msg[] = {0x1, amt};
  state->money -= amt;
  write(channels[state->id][to][0], msg, sizeof(msg));
}

void process_run(process_t* p) {
  srand(seed + p->id);  // so that each process generates unique random numbers

  // leave open only the channels relevant to this process
  int i, j;
  for (i = 0; i < num_processes; ++i) {
    for (j = 0; j < num_processes; ++j) {
      if (i == j) {
        // the channels on the diagonal are useless
        close(channels[i][j][0]);
        close(channels[i][j][1]);
      } else if (i == p->id) {
        // we care only about the sending end, close the receiving end
        close(channels[i][j][1]);
      } else if (j == p->id) {
        // we care only about the receiving end, close the sending end
        close(channels[i][j][0]);
      } else {
        // this channel has nothing to do with us
        close(channels[i][j][0]);
        close(channels[i][j][1]);
      }
    }
  }

  // we will send messages out on channels[id][*][0] and receive messages on
  // channels[*][id][1]
  
  // construct a poll set for reading
  struct pollfd* fds = malloc(sizeof(*fds) * num_processes);
  for (i = 0; i < num_processes; ++i) {
    if (i == p->id) {
      fds[i].fd = -1;
    } else {
      fds[i].fd = channels[i][p->id][1];
      fds[i].events = POLLIN;
    }
  }

  while (1) {
    // randomly decide to send or receive a message
    int choice = randn(5);
    if (choice) {  // send
      process_send_message(p, random_process(p->id));
    } else {  // receive
      int wait_for = randn(300);  // ms
      poll(fds, num_processes - 1, wait_for);
      for (i = 0; i < num_processes - 1; ++i) {
        if (fds[i].revents & POLLIN) {
          process_handle_message(p, fds[i].fd);
        }
      }
    }

    sleep(1);
    printf("process %d's money: %d\n", p->id, p->money);
    sleep(1);
  }
}

/* Our model: we have one "driver" process (the main process) responsible for 
 * spawning the relevant sub-processes and establishing the channels between
 * them. The processes themselves then do the communication and, eg, timestamp
 * assigning.
 */
int main(int argc, char** argv) {
  parse_flags(argc, argv);


  int i, j;  // loop indicies

  /* 
   * channels[i][j] is the channel from process i to process j, with
   * channels[i][j][0] being the i's end and channels[i][j][1] being j's end.
   *
   * Ie, if process i wishes to talk to process j, it will send a message on
   * channels[i][j][0], and if process j wishes to receive such a message from
   * process i, it will read from channels[i][j][1].
   *
   * channels[i][i][0] and channels[i][i][1], for each i, is, of course,
   * wasted, for the sake of a simple indexing scheme.
   */
  channels = malloc(sizeof(int**) * num_processes);
  for (i = 0; i < num_processes; ++i) {
    channels[i] = malloc(sizeof(int*) * num_processes);
    for (j = 0; j < num_processes; ++j) {
      channels[i][j] = malloc(sizeof(int) * 2);
      socketpair(PF_LOCAL, SOCK_STREAM, 0, channels[i][j]);
    }
  }

  for (i = 0; i < num_processes; ++i) {
    if (fork() == 0) {
      process_t p;
      process_init(&p, i);
      process_run(&p);
      exit(0);
    }
  }

  while (waitpid(-1, NULL, 0));

  // TODO: free channels

  return 0;
}
