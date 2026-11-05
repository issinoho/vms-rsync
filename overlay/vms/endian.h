/* endian.h - what openrsync takes from <endian.h>, for OpenVMS (vms-rsync).
   x86-64 and IA64 are both little-endian. */
#ifndef VMS_ENDIAN_H
#define VMS_ENDIAN_H
#include <stdint.h>
#define LITTLE_ENDIAN 1234
#define BIG_ENDIAN    4321
#define BYTE_ORDER    LITTLE_ENDIAN
#define htole16(x) ((uint16_t) (x))
#define le16toh(x) ((uint16_t) (x))
#define htole32(x) ((uint32_t) (x))
#define le32toh(x) ((uint32_t) (x))
#define htole64(x) ((uint64_t) (x))
#define le64toh(x) ((uint64_t) (x))
#define htobe16(x) ((uint16_t) ((((x) & 0xff) << 8) | (((x) >> 8) & 0xff)))
#define be16toh(x) htobe16(x)
#define htobe32(x) ((uint32_t) ((((uint32_t) (x) & 0xffU) << 24) | (((uint32_t) (x) & 0xff00U) << 8) | \
                    (((uint32_t) (x) >> 8) & 0xff00U) | (((uint32_t) (x) >> 24) & 0xffU)))
#define be32toh(x) htobe32(x)
#endif
