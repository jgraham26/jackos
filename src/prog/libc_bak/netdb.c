#include <netdb.h>
#include <stdlib.h>

int h_errno = 0;

const char *gai_strerror(int errcode) {
    switch (errcode) {
        case 0:            return "Success";
        case EAI_BADFLAGS: return "Invalid value for ai_flags";
        case EAI_NONAME:   return "Name or service not known";
        case EAI_AGAIN:    return "Temporary failure in name resolution";
        case EAI_FAIL:     return "Non-recoverable failure in name resolution";
        case EAI_FAMILY:   return "Address family not supported";
        case EAI_SOCKTYPE: return "Socket type not supported";
        case EAI_SERVICE:  return "Service not supported for socket type";
        case EAI_MEMORY:   return "Memory allocation failure";
        case EAI_SYSTEM:   return "System error";
        default:           return "Unknown error";
    }
}

void freeaddrinfo(struct addrinfo *res) {
    while (res) {
        struct addrinfo *next = res->ai_next;
        if (res->ai_canonname) free(res->ai_canonname);
        if (res->ai_addr) free(res->ai_addr);
        free(res);
        res = next;
    }
}