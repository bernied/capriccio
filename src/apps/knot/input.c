#include <assert.h>
#include <string.h>
#include <unistd.h>

#include "input.h"


#ifndef DEBUG_input_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif

void
input_init(input_state *state, int socket)
{
    assert(state != NULL);

    state->used = 0;
    state->valid = 0;

    state->socket = socket;
}

char *
input_get_line(input_state *state)
{
    char *result = NULL;
    int done = 0;

    while (!done)
    {
        char *start;
        char *newline;

        // brief sanity check
        assert(0 <= state->used);
        assert(state->used <= state->valid);
        assert(state->valid <= INPUT_MAX_BUF);

        // make sure there's a null at the end of the string to search
        state->buf[state->valid] = 0;

        // look for a newline
        start = &state->buf[state->used];
        newline = strstr(start, "\r\n");

        if (newline != NULL)
        {
            // newline was found; return a ptr to the beginning of the line,
            // with null termination at the newline
            *newline = 0;
            result = start;
            done = 1;

            // updated used
            state->used = newline - state->buf + 2;

            // if there's no more valid data in the buffers, reset 'em
            if (state->used == state->valid)
            {
                state->used = 0;
                state->valid = 0;
            }
        }
        else if (state->valid < INPUT_MAX_BUF)
        {
            // there's still room to read data--fill it up.
            char *empty = &state->buf[state->valid];
            int n = read(state->socket, empty, INPUT_MAX_BUF - state->valid);

            if (n <= 0)
            {
                // on error or eof, we're done
                result = NULL;
                done = 1;
            }
            else
            {
                // we got more data, so loop again to look for a newline
                state->valid += n;
            }
        }
        else if (state->used > 0)
        {
            // move the unused data to the beginning of the buffer 
            memmove( &state->buf[0], &state->buf[state->used], state->valid - state->used );
            state->valid -= state->used;
            state->used = 0;
        }
        else
        {
            // state->used == 0 && state->valid == INPUT_MAX_BUF,
            // so the entire buffer is full of valid, unused data.
            // there's no newline in this data, though, so we bail.
            result = NULL;
            done = 1;
        }
    }
    
    return result;
}



//////////////////////////////////////////////////
// Set the emacs indentation offset
// Local Variables: ***
// c-basic-offset:4 ***
// End: ***
//////////////////////////////////////////////////
