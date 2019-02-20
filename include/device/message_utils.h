#ifndef MESSAGE_UTILS_H_
#define MESSAGE_UTILS_H_
#include <device/messages.h>

enum convert_to_endian_e {
    CONVERT_TO_NETWORK,
    CONVERT_TO_HOST,
};
typedef enum convert_to_endian_e convert_to_endian;

int convert_header(messageHeader_t *header, convert_to_endian convert_to);

#endif /* MESSAGE_UTILS_H_ */
