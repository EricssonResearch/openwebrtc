/*
 * Copyright (c) 2015, Ericsson AB. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

/*/
\*\ Owr crypto utils
/*/

/**
 * 
 *
 * Functions to get certificate, fingerprint and private key.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "owr_crypto_utils.h"
#include "owr_private.h"
#include "owr_utils.h"

#ifdef OWR_STATIC
#include <stdlib.h>
#endif

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#ifdef __ANDROID__
#include <android/log.h>
#endif

#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>

GST_DEBUG_CATEGORY_EXTERN(_owrcrypto_debug);
#define GST_CAT_DEFAULT _owrcrypto_debug

/* PUBLIC */

/**
 * owr_crypto_create_crypto_data:
 * @types:
 * @callback: (scope async):
 * @data: User data
 */

/**
 * OwrCaptureSourcesCallback:
 * @privatekey: (transfer none)
 * @certificate: (transfer none)
 * @fingerprint: (transfer none)
 * @fingerprint_function: (transfer none)
 * @data:
 *
 * Prototype for the callback passed to owr_get_capture_sources()
 */

typedef struct {
    OwrCryptoDataCallback callback;
    gpointer user_data;
} WorkerData;

typedef struct {
    WorkerData* worker_data;
    gboolean errorDetected;
    gchar* pem_key;
    gchar* pem_cert;
    gchar* char_fprint;
} CryptoData;

void owr_crypto_create_crypto_data(OwrCryptoDataCallback callback, gpointer data)
{
    GThread* crypto_worker;
    WorkerData* worker_data = g_new(WorkerData, 1);

    worker_data->callback = callback;
    worker_data->user_data = data;

    crypto_worker = g_thread_new("crypto_worker", _create_crypto_worker_run, (gpointer)worker_data);

    g_thread_unref(crypto_worker);
}

gpointer _create_crypto_worker_run(gpointer data)
{
    WorkerData* worker_data = (WorkerData*)data;

    g_return_val_if_fail(worker_data->callback, NULL);

    X509* cert;

    X509_NAME* name = NULL;

    EVP_PKEY* key_pair;

    RSA* rsa;

#define GST_DTLS_BIO_BUFFER_SIZE 4096
    BIO* bio_cert;
    gchar buffer_cert[GST_DTLS_BIO_BUFFER_SIZE] = { 0 };
    gint len_cert;
    gchar* pem_cert = NULL;
    BIO* bio_key;
    gchar buffer_key[GST_DTLS_BIO_BUFFER_SIZE] = { 0 };
    gint len_key;
    gchar* pem_key = NULL;

    gboolean errorDetected = FALSE;

    bio_cert = BIO_new(BIO_s_mem());
    bio_key = BIO_new(BIO_s_mem());

    GString* string_fprint = NULL;
    gchar* char_fprint = NULL;
    guint j;
    const EVP_MD* fprint_type = NULL;
    fprint_type = EVP_sha256();
    guchar fprint[EVP_MAX_MD_SIZE];

    guint fprint_size;

    cert = X509_new();

    key_pair = EVP_PKEY_new();

    // RSA_generate_key was deprecated in OpenSSL 0.9.8.
#if OPENSSL_VERSION_NUMBER < 0x10100001L
    rsa = RSA_generate_key(2048, RSA_F4, NULL, NULL);
#else
    rsa = RSA_new ();
    if (rsa != NULL) {
        BIGNUM *e = BN_new ();
        if (e == NULL || !BN_set_word(e, RSA_F4)
            || !RSA_generate_key_ex(rsa, 2048, e, NULL)) {
            RSA_free(rsa);
            rsa = NULL;
        }
        if (e)
            BN_free(e);
    }
#endif
    EVP_PKEY_assign_RSA(key_pair, rsa);

    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 0);
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), 31536000L); /* A year */
    X509_set_pubkey(cert, key_pair);

    name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (unsigned char*)"SE",
        -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
        (unsigned char*)"OpenWebRTC", -1, -1, 0);
    X509_set_issuer_name(cert, name);
    name = NULL;

    X509_sign(cert, key_pair, EVP_sha256());

    if (!X509_digest(cert, fprint_type, fprint, &fprint_size)) {
        GST_ERROR("Error, could not create certificate fingerprint");
        errorDetected = TRUE;
    }

    string_fprint = g_string_new(NULL);

    for (j = 0; j < fprint_size; j++) {
        g_string_append_printf(string_fprint, "%02X", fprint[j]);
        if (j + 1 != fprint_size) {
            g_string_append_printf(string_fprint, "%c", ':');
        }
    }

    char_fprint = g_string_free(string_fprint, FALSE);

    if (!PEM_write_bio_X509(bio_cert, (X509*)cert)) {
        GST_ERROR("Error, could not write certificate bio");
        errorDetected = TRUE;
    }

    if (!PEM_write_bio_PrivateKey(bio_key, (EVP_PKEY*)key_pair, NULL, NULL, 0, 0, NULL)) {
        GST_ERROR("Error, could not write PrivateKey bio");
        errorDetected = TRUE;
    }

    len_cert = BIO_read(bio_cert, buffer_cert, GST_DTLS_BIO_BUFFER_SIZE);
    if (!len_cert) {
        GST_ERROR("Error, no certificate length");
        errorDetected = TRUE;
    }

    len_key = BIO_read(bio_key, buffer_key, GST_DTLS_BIO_BUFFER_SIZE);
    if (!len_key) {
        GST_ERROR("Error, no key length");
        errorDetected = TRUE;
    }

    pem_cert = g_strndup(buffer_cert, len_cert);
    pem_key = g_strndup(buffer_key, len_key);

    CryptoData* report_data = g_new0(CryptoData, 1);

    report_data->worker_data = worker_data;
    report_data->errorDetected = errorDetected;
    report_data->pem_key = pem_key;
    report_data->pem_cert = pem_cert;
    report_data->char_fprint = char_fprint;

    g_idle_add(_create_crypto_worker_report, (gpointer)report_data);

    // some cleanup

    //RSA_free(rsa);  -- gives segmentation fault about every second time

    X509_free(cert);
    BIO_free(bio_cert);
    BIO_free(bio_key);
    EVP_PKEY_free(key_pair);

    return NULL;
}

gboolean _create_crypto_worker_report(gpointer data)
{

    CryptoData* report_data = (CryptoData*)data;
    guint i;
    GClosure* closure;

    closure = g_cclosure_new(G_CALLBACK(report_data->worker_data->callback), NULL, NULL);
    g_closure_set_marshal(closure, g_cclosure_marshal_generic);

    GValue params[5] = { G_VALUE_INIT, G_VALUE_INIT, G_VALUE_INIT, G_VALUE_INIT, G_VALUE_INIT };

    for (i = 0; i < 4; i++)
        g_value_init(&params[i], G_TYPE_STRING);
    g_value_init(&params[i], G_TYPE_POINTER);

    g_value_set_pointer(&params[4], report_data->worker_data->user_data);

    if (report_data->errorDetected) {
        GST_ERROR("Returning with error");
        for (i = 0; i < 4; i++)
            g_value_set_string(&params[i], "Failure");

        g_closure_invoke(closure, NULL, 5, (const GValue*)&params, NULL);
    }
    else {
        g_value_set_string(&params[0], report_data->pem_key);
        g_value_set_string(&params[1], report_data->pem_cert);
        g_value_set_string(&params[2], report_data->char_fprint);
        g_value_set_string(&params[3], "sha-256");

        g_closure_invoke(closure, NULL, 5, (const GValue*)&params, NULL);
    }

    // some cleanup
    for (i = 0; i < 5; i++)
        g_value_unset(&params[i]);

    g_closure_unref(closure);

    g_free(report_data->worker_data);
    g_free(report_data->pem_key);
    g_free(report_data->pem_cert);
    g_free(report_data->char_fprint);
    g_free(report_data);

    return FALSE;
}
