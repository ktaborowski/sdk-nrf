/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <psa/crypto.h>
#include <psa/crypto_extra.h>

#ifdef CONFIG_BUILD_WITH_TFM
#include <tfm_ns_interface.h>
#endif

#include "trusted_storage_init.h"

#define APP_SUCCESS	    (0)
#define APP_ERROR	    (-1)
#define APP_SUCCESS_MESSAGE "Example finished successfully!"
#define APP_ERROR_MESSAGE   "Example exited with error!"

#define PRINT_HEX(p_label, p_text, len)                                                            \
	({                                                                                         \
		LOG_INF("---- %s (len: %u): ----", p_label, len);                                  \
		LOG_HEXDUMP_INF(p_text, len, "Content:");                                          \
		LOG_INF("---- %s end  ----", p_label);                                             \
	})

LOG_MODULE_REGISTER(ecdsa, LOG_LEVEL_DBG);

/* ====================================================================== */
/*				Global variables/defines for the ECDSA example			  */

#define NRF_CRYPTO_EXAMPLE_ECDSA_TEXT_SIZE (100)

#define NRF_CRYPTO_EXAMPLE_ECDSA_PUBLIC_KEY_SIZE (65)
#define NRF_CRYPTO_EXAMPLE_ECDSA_SIGNATURE_SIZE	 (64)
#define NRF_CRYPTO_EXAMPLE_ECDSA_HASH_SIZE	 (32)

/* Below text is used as plaintext for signing/verification */
static uint8_t m_plain_text[NRF_CRYPTO_EXAMPLE_ECDSA_TEXT_SIZE] = {
	"Example string to demonstrate basic usage of ECDSA."};

static uint8_t m_signature[NRF_CRYPTO_EXAMPLE_ECDSA_SIGNATURE_SIZE];

static psa_key_id_t priv_key_id = PSA_KEY_ID_USER_MIN + 1;
/* ====================================================================== */

int crypto_init(void)
{
	psa_status_t status;

#ifdef CONFIG_TRUSTED_STORAGE_BACKEND_AEAD_KEY_DERIVE_FROM_HUK
	write_huk();
#endif /* CONFIG_TRUSTED_STORAGE_BACKEND_AEAD_KEY_DERIVE_FROM_HUK */

	/* Initialize PSA Crypto */
	status = psa_crypto_init();
	if (status != PSA_SUCCESS) {
		LOG_INF("psa_crypto_init failed! (Error: %d)", status);
		return APP_ERROR;
	}

	return APP_SUCCESS;
}

int crypto_finish(void)
{
	psa_status_t status;

	/* Destroy the key handle */
	status = psa_destroy_key(priv_key_id);
	if (status != PSA_SUCCESS) {
		LOG_INF("psa_destroy_key failed! (Error: %d)", status);
		return APP_ERROR;
	}

	return APP_SUCCESS;
}

int prepare_ecdsa_private_key(void)
{
	psa_status_t status;
	psa_key_id_t id;

	uint8_t raw_key[] = {0x2d, 0x34, 0x22, 0x89, 0xd1, 0x5c, 0x21, 0x87, 0x8c, 0x05, 0xc9,
			     0x10, 0x58, 0x1a, 0x85, 0x49, 0x5e, 0x49, 0x66, 0xe6, 0xeb, 0x71,
			     0x67, 0x6d, 0xde, 0x44, 0x51, 0x5b, 0x15, 0x2b, 0x81, 0x9f};

	LOG_INF("Prepare ECDSA private key...");

	/* Configure the key attributes */
	psa_key_attributes_t key_attributes = PSA_KEY_ATTRIBUTES_INIT;

	/* Configure the key attributes */
	psa_set_key_usage_flags(&key_attributes, PSA_KEY_USAGE_SIGN_HASH);
	psa_set_key_algorithm(&key_attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
	psa_set_key_type(&key_attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
	psa_set_key_bits(&key_attributes, 256);

	/* Persistent key specific settings */
	psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_PERSISTENT);
	psa_set_key_id(&key_attributes, priv_key_id);

	/* Generate a key pair */
	status = psa_import_key(&key_attributes, raw_key, sizeof(raw_key), &id);
	if (status != PSA_SUCCESS) {
		LOG_INF("psa_import_key failed! (Error: %d)", status);
		return APP_ERROR;
	}
	if (priv_key_id != id) {
		LOG_ERR("Invalid key id %d != %d", priv_key_id, id);
	}

	/* Make sure the key is not in memory anymore, has the same affect then resetting the device
	 */
	status = psa_purge_key(priv_key_id);
	if (status != PSA_SUCCESS) {
		LOG_INF("psa_purge_key failed! (Error: %d)", status);
		return APP_ERROR;
	}

	/* Reset key attributes and free any allocated resources. */
	psa_reset_key_attributes(&key_attributes);

	return APP_SUCCESS;
}

int sign_message(void)
{
	uint32_t output_len;
	psa_status_t status;

	LOG_INF("Signing a message using ECDSA...");

	status = psa_sign_message(priv_key_id, PSA_ALG_ECDSA(PSA_ALG_SHA_256), m_plain_text,
				  sizeof(m_plain_text), m_signature, sizeof(m_signature),
				  &output_len);
	if (status != PSA_SUCCESS) {
		LOG_INF("psa_sign_hash failed! (Error: %d)", status);
		return APP_ERROR;
	}

	LOG_INF("Message signed successfully!");
	PRINT_HEX("Plaintext", m_plain_text, sizeof(m_plain_text));
	PRINT_HEX("Signature", m_signature, sizeof(m_signature));

	return APP_SUCCESS;
}

int main(void)
{
	int status;

	LOG_INF("Starting ECDSA example...");

	status = crypto_init();
	if (status != APP_SUCCESS) {
		LOG_INF(APP_ERROR_MESSAGE);
		return APP_ERROR;
	}

	status = prepare_ecdsa_private_key();
	if (status != APP_SUCCESS) {
		LOG_INF(APP_ERROR_MESSAGE);
		return APP_ERROR;
	}

	status = sign_message();
	if (status != APP_SUCCESS) {
		LOG_INF(APP_ERROR_MESSAGE);
		return APP_ERROR;
	}

	status = crypto_finish();
	if (status != APP_SUCCESS) {
		LOG_INF(APP_ERROR_MESSAGE);
		return APP_ERROR;
	}

	LOG_INF(APP_SUCCESS_MESSAGE);

	return APP_SUCCESS;
}
