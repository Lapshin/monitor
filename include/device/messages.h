#ifndef DEVICE_TRANSFER_H_
#define DEVICE_TRANSFER_H_
#include <stdlib.h>
#include <device/types.h>

#define PROTOCOL_VERSION 1

#define DEFAILT_IP "127.0.0.1"
#define DEFAULT_PORT 5000

#define PACKET_MAGIC 0xDEADBEEF
#define DEVICE_NAME_LEN 16


typedef enum read_message_status_e
{
    READ_STATUS__BAD_MAGIC = -1,
    READ_STATUS__UNCKNOWN_VERSION = -2,
    READ_STATUS__UNCKNOWN_TYPE = -3,
    READ_STATUS__BAD_SIZE = -4,
    READ_STATUS__EOF = -5,
    READ_STATUS__NO_DATA = -6,
    READ_STATUS__ERRNO = -7,
    READ_STATUS__MEMORY_ERROR = -8,
    READ_STATUS__OK = 0,
} read_message_status_t;

typedef enum message_type_e {
    MSG_TYPE__NAME,
    MSG_TYPE__DATA,

    MSG_TYPE__MAX,
} message_type_t;

extern const size_t message_sizes[MSG_TYPE__MAX];

struct messageHeader_s 
{
    u32 magic;
    u32 version;
    u32 size;
    u32 type;
    u8 reserved[32];
} __attribute__ ((__packed__));
typedef struct messageHeader_s messageHeader_t;


struct messageGeneric_s 
{
    messageHeader_t header;
    u8 payload[]; 
} __attribute__ ((__packed__));
typedef struct messageGeneric_s messageGeneric_t;

struct messageDeviceName_s 
{
    messageHeader_t header;
    u8 name[DEVICE_NAME_LEN];
} __attribute__ ((__packed__));
typedef struct messageDeviceName_s messageDeviceName_t;

struct messageDeviceData_s 
{
    messageHeader_t header;
    u32 data;
} __attribute__ ((__packed__));
typedef struct messageDeviceData_s messageDeviceData_t;

struct messageBiggest_s 
{
    union {
        messageDeviceData_t device_data;
        messageDeviceName_t device_name;
    };
} __attribute__ ((__packed__));
typedef struct messageBiggest_s messageBiggest_t;



#endif /* DEVICE_TRANSFER_H_ */
