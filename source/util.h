#ifndef UTIL_H
#define UTIL_H

#define ARRAY_SIZE(ARRAY)  (sizeof(ARRAY) / sizeof((ARRAY)[0]))

#ifndef _WIN32
#define stricmp strcasecmp
#define strnicmp strncasecmp

#define MAX_PATH 512
#endif


#endif
