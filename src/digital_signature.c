//
// Created by Anne Shirley on 4/24/2022.
//

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/bn.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>
#include <openssl/pem.h>
#include "digital_signature.h"

unsigned char * sha256digest (const void *message, const int message_length) {
    unsigned char * buffer = malloc (SHA256_DIGEST_LENGTH);
    SHA256 (message, message_length, buffer);
    return buffer;
}

static void free_signature (struct dsa_signature_t *data) {
    if (data != NULL) {
        if (data->signature != NULL) {
            free (data->signature);
        }
        free (data);
    }
}

static void print_signature (struct dsa_signature_t *signature) {
    uint32_t i;

    for (i = 0; i < signature->length; i++) printf ("%02x", (uint8_t)signature->signature[i]);
}

struct dsa_signature_t *new_dsa_signature (void) {
    struct dsa_signature_t *data = malloc (sizeof (struct dsa_signature_t));

    data->signature = NULL;
    data->length = 0;
    data->dispose = free_signature;
    data->print = print_signature;

    return data;
}
