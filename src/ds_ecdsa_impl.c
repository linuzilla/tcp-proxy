//
// Created by Anne Shirley on 4/24/2022.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/bn.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>
#include <openssl/pem.h>
#include "digital_signature.h"

struct ecdsa_helper_data_t {
    EC_KEY *private_key;
    EC_KEY *public_key;
    int curve_name;
};

static EC_KEY *create_key (const char *key, EC_KEY * (*func) (BIO *, EC_KEY **, pem_password_cb *, void *)) {
    BIO *bio = BIO_new_mem_buf ((void *) key, -1);

    if (bio == NULL) {
        return NULL;
    } else {
        EC_KEY *ec_key = func (bio, NULL, NULL, NULL);
        BIO_free (bio);
        return ec_key;
    }
}

static struct dsa_signature_t *sign_message (struct dsa_helper_t *self, const void *message, const int message_length) {
    struct ecdsa_helper_data_t *data = (struct ecdsa_helper_data_t *) self->data;

    if (data != NULL && data->private_key != NULL) {

        struct dsa_signature_t *signature = new_dsa_signature();
//        unsigned char buffer_digest[SHA256_DIGEST_LENGTH];
        // Sign message
        signature->length = ECDSA_size (data->private_key); // the signature size depends on the key
        signature->signature = malloc (signature->length);

        printf ("signature length predict: %d\n", signature->length);

        uint8_t *digest = (uint8_t *) sha256digest (message, message_length);
//        uint8_t *digest = SHA256(message, message_length, buffer_digest);
        int rc = ECDSA_sign (0, digest, SHA256_DIGEST_LENGTH,
                             (uint8_t *) signature->signature, &signature->length, data->private_key);

        printf ("sign message: %s, signature len: %d\n", rc ? "successful" : "failed", signature->length);

//        printf("Message SHA256: ");
//        for (uint32_t i = 0; i < SHA256_DIGEST_LENGTH; i++) printf("%02x", digest[i]);
//        printf("\n");

        free (digest);

        return signature;
    }

    return NULL;
}

static bool verify_signature (struct dsa_helper_t *self, const void *message, const int message_length,
                              struct dsa_signature_t *signature) {
    struct ecdsa_helper_data_t *data = (struct ecdsa_helper_data_t *) self->data;

    int verification = 0;

    if (data != NULL) {
        uint8_t *digest = (uint8_t *) sha256digest (message, message_length);

        if (data->public_key != NULL) {
            verification = ECDSA_verify (0, digest, SHA256_DIGEST_LENGTH, (uint8_t *) signature->signature,
                                         signature->length, data->public_key);
        } else if (data->private_key != NULL) {
            // Verify the signature
            verification = ECDSA_verify (0, digest, SHA256_DIGEST_LENGTH, (uint8_t *) signature->signature,
                                         signature->length, data->private_key);
        }
        free (digest);
    }

    return verification;
}

static bool load_ec_public_key (struct dsa_helper_t *self, const int curve_name, const char *public_key_hex) {
    struct ecdsa_helper_data_t *data = (struct ecdsa_helper_data_t *) self->data;

    if (data->public_key == NULL) {
        EC_KEY_free (data->public_key);
        data->public_key = NULL;
    }
    data->public_key = EC_KEY_new_by_curve_name (curve_name);
    EC_GROUP *curve_group = EC_GROUP_new_by_curve_name (curve_name);
    EC_POINT *public_point = EC_POINT_new (curve_group);
    public_point = EC_POINT_hex2point (curve_group, public_key_hex, public_point, NULL);
    int rc = EC_KEY_set_public_key (data->public_key, public_point);
    EC_GROUP_free (curve_group);
    EC_POINT_free (public_point);

    if (rc == 0) {
        EC_KEY_free (data->public_key);
        data->public_key = NULL;
        return false;
    }
    return true;
}

static int ec_curve_name (struct dsa_helper_t *self) {
    struct ecdsa_helper_data_t *data = (struct ecdsa_helper_data_t *) self->data;
    return data->curve_name;
}

static bool have_private_key (struct dsa_helper_t *self) {
    return ((struct ecdsa_helper_data_t *) self->data)->private_key != NULL;
}

static bool have_public_key (struct dsa_helper_t *self) {
    return ((struct ecdsa_helper_data_t *) self->data)->public_key != NULL;
}

static void clear_private_key (struct dsa_helper_t *self) {
    struct ecdsa_helper_data_t *data = (struct ecdsa_helper_data_t *) self->data;

    if (data->private_key != NULL) {
        EC_KEY_free (data->private_key);
        data->private_key = NULL;
    }
}

static void clear_public_key (struct dsa_helper_t *self) {
    struct ecdsa_helper_data_t *data = (struct ecdsa_helper_data_t *) self->data;

    if (data->public_key != NULL) {
        EC_KEY_free (data->public_key);
        data->public_key = NULL;
    }
}

static void dispose (struct dsa_helper_t *self) {
    if (self != NULL) {
        if (self->data != NULL) {
            struct ecdsa_helper_data_t *data = (struct ecdsa_helper_data_t *) self->data;

            clear_private_key (self);
            clear_public_key (self);

            free (data);
            self->data = NULL;
        }
        free (self);
    }
}

static struct dsa_helper_t instance = {
    .data = NULL,
    .sign = sign_message,
    .verify = verify_signature,
    .ec_curve_name = ec_curve_name,
    .load_ec_public_key = load_ec_public_key,
    .have_private_key = have_private_key,
    .have_public_key = have_public_key,
    .clear_private_key = clear_private_key,
    .clear_public_key = clear_public_key,
    .dispose = dispose,
};


struct dsa_helper_t *new_ecdsa (const char *private_key, const char *public_key) {
    struct dsa_helper_t *self = malloc (sizeof (struct dsa_helper_t));

    while (self != NULL) {
        memcpy (self, &instance, sizeof (struct dsa_helper_t));
        struct ecdsa_helper_data_t *data = malloc (sizeof (struct ecdsa_helper_data_t));

        self->data = data;

        if (self->data == NULL) {
            dispose (self);
            self = NULL;
            break;
        }

        data->private_key = private_key != NULL ? create_key (private_key, PEM_read_bio_ECPrivateKey) : NULL;
        data->public_key = public_key != NULL ? create_key (public_key, PEM_read_bio_EC_PUBKEY) : NULL;
        data->curve_name = 0;

        if (data->private_key != NULL) {
            // Get private key
            const BIGNUM *priv_key = (BIGNUM *) EC_KEY_get0_private_key (data->private_key);
            const char *priv_key_char = BN_bn2hex (priv_key);


            // Get public key
            const EC_POINT *pub_key = (EC_POINT *) EC_KEY_get0_public_key (data->private_key);
            const EC_GROUP *curve_group = EC_KEY_get0_group (data->private_key);
//    secp256k1_group = EC_GROUP_new_by_curve_name (NID_secp256k1);
            char *pub_key_char = EC_POINT_point2hex (curve_group, pub_key, POINT_CONVERSION_COMPRESSED, NULL);
//    EC_GROUP_free (group);
            data->curve_name = EC_GROUP_get_curve_name (curve_group);

            printf ("Private key: %s\n", priv_key_char);
            printf ("Public key: %s\n", pub_key_char);
            printf ("Curve Name: %d\n", data->curve_name);
        }
        break;
    }

    return self;
}