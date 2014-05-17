#ifndef HTTP_H
#define HTTP_H

#include "input.h"

typedef enum http_version http_version;
enum http_version
{
    HTTP_VERSION_1_0,
    HTTP_VERSION_1_1,
};

typedef struct http_request http_request;
struct http_request
{
    char url[80];
    http_version version;
    int socket;
    int closed;
};

void
http_init(http_request *req, int socket);

int
http_parse(http_request *req);

#endif
