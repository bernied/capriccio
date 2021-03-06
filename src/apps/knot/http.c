#include <assert.h>
#include <string.h>

#include "http.h"
#include "input.h"

#ifndef DEBUG_http_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif


#define STR_METHOD_GET "GET"
#define STR_VERSION_1_0 "HTTP/1.0"
#define STR_VERSION_1_1 "HTTP/1.1"

void
http_init(http_request *request, int socket)
{
    request->url[0] = 0;
    request->version = HTTP_VERSION_1_0;
    request->closed = 0;
    request->socket = socket;
}

int
http_parse(http_request *request)
{
    int result = 0;
    int done = 0;
    input_state state;

    input_init(&state, request->socket);

    request->url[0] = 0;
    request->version = HTTP_VERSION_1_0;

    while (!done)
    {
        char *line = input_get_line(&state);
        
        if (line == NULL)
        {
            request->closed = 1;
            result = 0;
            done = 1;
        }
        else if (line[0] == 0)
        {
            result = (request->url[0] != 0);
            done = 1;
        }
        else if (request->url[0] == 0)
        {
            char *method;
            char *url;
            char *protocol;
            http_version version = HTTP_VERSION_1_0;
            int valid = 0;
            
            method = line;
            url = strchr(line, ' ');

            if (url != NULL)
            {
                *url++ = 0;
                protocol = strchr(url, ' ');

                if (protocol != NULL)
                {
                    *protocol++ = 0;

                    if (strcmp(method, STR_METHOD_GET) == 0)
                    {
                        if (strcmp(protocol, STR_VERSION_1_1) == 0)
                        {
                            version = HTTP_VERSION_1_1;
                            valid = 1;
                        }
                        else if (strcmp(protocol, STR_VERSION_1_0) == 0)
                        {
                            version = HTTP_VERSION_1_0;
                            valid = 1;
                        }
                    }
                }
            }

            if (valid)
            {
                url -= 1;

                assert(line <= url);
                url[0] = '.';

                // save the URL.  This is necessary b/c the get_line()
                // function returns a pointer into it's internal
                // buffer.
                assert(strlen(url) < sizeof(request->url));
                strcpy(request->url, url);
                assert(request->url[0] == '.');

                request->version = version;
            }
            else
            {
                result = 0;
                done = 1;
            }
        }
        else
        {
            // parse other headers...
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
