#include <arpa/inet.h>
#include <device/message_utils.h>

const size_t message_sizes[MSG_TYPE__MAX] =
        {
                sizeof(messageDeviceName_t),
                sizeof(messageDeviceData_t),
        };

int convert_header(messageHeader_t *header, convert_to_endian convert_to)
{
    if(header == NULL)
    {
        return -1;
    }
    if(convert_to != CONVERT_TO_HOST && convert_to != CONVERT_TO_NETWORK)
    {
        return -1;
    }
    header->magic =
            (convert_to == CONVERT_TO_NETWORK)?
                    htonl(header->magic) : ntohl(header->magic);
    header->version =
                (convert_to == CONVERT_TO_NETWORK)?
                        htonl(header->version) : ntohl(header->version);
    header->type =
            (convert_to == CONVERT_TO_NETWORK)?
                    htonl(header->type) : ntohl(header->type);
    header->size =
            (convert_to == CONVERT_TO_NETWORK)?
                    htonl(header->size) : ntohl(header->size);

    return 0;
}
