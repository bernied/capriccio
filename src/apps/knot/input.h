#ifndef INPUT_H
#define INPUT_H

#define INPUT_MAX_BUF 511

typedef struct input_state input_state;
struct input_state
{
    char buf[INPUT_MAX_BUF + 1];

    int used;
    int valid;

    int socket;
};

void
input_init(input_state *state, int socket);

char *
input_get_line(input_state *state);

#endif
