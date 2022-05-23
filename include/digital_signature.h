//
// Created by Anne Shirley on 4/24/2022.
//

#ifndef DIGITAL_SIGNATURE_H
#define DIGITAL_SIGNATURE_H

#include <stdbool.h>

struct dsa_signature_t {
    char *signature;
    unsigned int length;
    void (*dispose) (struct dsa_signature_t *);
    void (*print) (struct dsa_signature_t *);
};

struct dsa_helper_t {
    void *data;

    struct dsa_signature_t * (*sign) (struct dsa_helper_t *self, const void *message, const int message_length);
    bool (*verify) (struct dsa_helper_t *self, const void *message, const int message_length, struct dsa_signature_t *signature);
    int (*ec_curve_name) (struct dsa_helper_t *self);
    bool (*load_ec_public_key) (struct dsa_helper_t *self, const int curve_name, const char *public_key_hex);
    bool (*have_private_key) (struct dsa_helper_t *self);
    bool (*have_public_key) (struct dsa_helper_t *self);
    void (*clear_private_key) (struct dsa_helper_t *self);
    void (*clear_public_key) (struct dsa_helper_t *self);
    void (*dispose) (struct dsa_helper_t *self);
};


unsigned char * sha256digest (const void *message, const int message_length);
struct dsa_signature_t *new_dsa_signature (void);

// ECDSA
struct dsa_helper_t *new_ecdsa (const char *private_key, const char *public_key);

// RSA
struct dsa_helper_t *new_rsa_ds (const char *private_key, const char *public_key);

#endif //DIGITAL_SIGNATURE_H
