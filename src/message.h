#ifndef __MESSAGE_H__
#define __MESSAGE_H__

#include "common.h"

#define ID_MAX (63)

/*
** emessage -- external messages
** Meant to go over the wire.
*/

#define EMSG_PROTOCOL_IDENTIFY (1)
#define EMSG_PROTOCOL_DENY_DUP (2)
#define EMSG_CHAT              (100)
#define EMSG_BUFFER            (101)
#define EMSG_JUMP              (102)

typedef struct {
    uint8_t kind;
} ck_emessage;


/*
** imessage -- internal messages
** Meant to be converted to/from emessages and used by
** the yed interface.
*/

typedef struct {
    uint8_t  kind;
    int      user_id_len;
    char     user_id[ID_MAX];
    int      content_len;
    char    *contents; /* malloc()'d buffer */
} ck_imessage;

#endif
