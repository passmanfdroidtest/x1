/**
 *  Passman Android App
 *
 * @copyright Copyright (c) 2016, Sander Brand (brantje@gmail.com)
 * @copyright Copyright (c) 2016, Marcos Zuriaga Miguel (wolfi@wolfi.es)
 * @license GNU AGPL version 3 or any later version
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "SJCL.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <JSON.h>
#include "json.hpp"
#include "base64.h"
#include <android/log.h>
#include <vector>
#include <algorithm>
#include <cstdlib>

#define LOG_TAG "SJCL"

void handleErrors(const char* error){
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, (const char*)"FUCK THIS SHIT GOT AN ERROR: %s", error);
}

int decryptccm(unsigned char *ciphertext, int ciphertext_len, unsigned char *aad,
               int aad_len, unsigned char *tag, unsigned char *key, unsigned char *iv,
               unsigned char *plaintext
) {
    EVP_CIPHER_CTX *ctx;
    int len;
    int plaintext_len = -1;
    int ret = -1;
    int lol = 2;

    if (ciphertext_len >= 1<<16) lol++;
    if (ciphertext_len >= 1<<24) lol++;

    /* Create and initialise the context */
    if (!(ctx = EVP_CIPHER_CTX_new())) handleErrors("Error initializing context");

    /* Initialise the decryption operation. */
    if(1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_ccm(), nullptr, nullptr, nullptr)) {
        handleErrors("Error setting crypto mode");
        goto CLEANUP;
    }

    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_CCM_SET_IVLEN, 15-lol, nullptr)) {
        handleErrors("Error setting IV Length");
        goto CLEANUP;
    }

    /* Set expected tag value. */
    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_CCM_SET_TAG, 8, tag)) {
        handleErrors("Error setting TAG value");
        goto CLEANUP;
    }

    /* Initialise key and IV */
    if (1 != EVP_DecryptInit_ex(ctx, nullptr, nullptr, key, iv)) {
        handleErrors("Error setting KEY and IV");
        goto CLEANUP;
    }

    /* Provide the total ciphertext length
     */
    if (1 != EVP_DecryptUpdate(ctx, nullptr, &len, nullptr, ciphertext_len)) {
        handleErrors("Error setting cyphertext length");
        goto CLEANUP;
    }

    /* Provide any AAD data. This can be called zero or more times as
     * required
     */
    if (1 != EVP_DecryptUpdate(ctx, nullptr, &len, aad, aad_len)) {
        handleErrors("Error setting AAD data");
        goto CLEANUP;
    }

    /* Provide the message to be decrypted, and obtain the plaintext output.
     * EVP_DecryptUpdate can be called multiple times if necessary
     */
    ret = EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, ciphertext_len);

    plaintext_len = len;

    CLEANUP:
    /* Clean up */
    EVP_CIPHER_CTX_free(ctx);

    if(ret > 0)
    {
        /* Success */
        return plaintext_len;
    }
    else
    {
        /* Verify failed */
        return -1;
    }
}

int encryptccm(unsigned char *plaintext, int plaintext_len,
                unsigned char *aad, int aad_len,
                unsigned char *key,
                unsigned char *iv,
                int iv_len,
                unsigned char *ciphertext,
                unsigned char *tag,
                int tag_len)
{
    EVP_CIPHER_CTX *ctx;

    int len = -1;

    int ciphertext_len = -1;


    /* Create and initialise the context */
    if (!(ctx = EVP_CIPHER_CTX_new())) {
        handleErrors("Error initializing context");
        goto CLEANUP;
    }

    /* Initialise the encryption operation. */
    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_ccm(), nullptr, nullptr, nullptr)) {
        handleErrors("Error setting crypto mode");
        goto CLEANUP;
    }

    /* Set IV length */
    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_CCM_SET_IVLEN, iv_len, nullptr)) {
        handleErrors("Error setting IV Length");
        goto CLEANUP;
    }

    /* Set tag length */
    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_CCM_SET_TAG, tag_len, nullptr)) {
        handleErrors("Error setting tag length");
        goto CLEANUP;
    }

    /* Initialise key and IV */
    if (1 != EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, iv)) {
        handleErrors("Error setting KEY and IV");
        goto CLEANUP;
    }

    /* Provide the total plaintext length */
    if (1 != EVP_EncryptUpdate(ctx, nullptr, &len, nullptr, plaintext_len)) {
        handleErrors("Error setting plaintext length");
        goto CLEANUP;
    }

    /* Provide any AAD data. This can be called zero or one times as required */
    if (1 != EVP_EncryptUpdate(ctx, nullptr, &len, aad, aad_len)) {
        handleErrors("Error setting AAD data");
        goto CLEANUP;
    }

    /*
     * Provide the message to be encrypted, and obtain the encrypted output.
     * EVP_EncryptUpdate can only be called once for this.
     */
    if (
            1 != EVP_EncryptUpdate(
                ctx,
                ciphertext,
                &len,
                reinterpret_cast<const unsigned char *>(plaintext),
                plaintext_len
            )
    ) {
        handleErrors("Error obtaining the encrypted output");
        len = -1;
        goto CLEANUP;
    }

    ciphertext_len = len;

    /*
     * Finalise the encryption. Normally ciphertext bytes may be written at
     * this stage, but this does not occur in CCM mode.
     */
    if (1 != EVP_EncryptFinal_ex(ctx, ciphertext + len, &len)) {
        handleErrors("Error finalizing the encryption");
        len = ciphertext_len = -1;
        goto CLEANUP;
    }

    ciphertext_len += len;

    /* Get the tag */
    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_CCM_GET_TAG, tag_len, tag)) {
        handleErrors("Error getting the encryption tag");
        ciphertext_len = -1;
        goto CLEANUP;
    }

    /* Clean up */
    CLEANUP:
    EVP_CIPHER_CTX_free(ctx);

    return ciphertext_len;
}

/**
 * Casts an WString to an standard char array, beware, it does not care about encoding!
 * It just discards the wide part of the chars!
 */
char* wstring_to_char(wstring str) {
    char* c = (char *) malloc(sizeof(char) * str.length() + 1);
    const wchar_t* data = str.c_str();
    for (int i = 0; i <= str.length(); i++) {
        c[i] = (char) data[i];
    }
    return c;
}

using namespace WLF::Crypto;

char* SJCL::decrypt(string sjcl_json, string key) {
    JSONValue *data = JSON::Parse(sjcl_json.c_str());

    if (data == nullptr || ! data->IsObject()) {
        __android_log_write(ANDROID_LOG_ERROR, LOG_TAG, "Error parsing the SJCL JSON");
        return nullptr;
    }

    JSONObject food = data->AsObject();

    int iter        = 0,
        key_size    = 0,
        tag_size    = 0;
    char *iv_64, *salt_64, *cyphertext, *adata;

    // Extract the requiered values from the JSON
    iv_64       = wstring_to_char(food[L"iv"]->AsString());
    adata       = wstring_to_char(food[L"adata"]->AsString());
    salt_64     = wstring_to_char(food[L"salt"]->AsString());
    cyphertext  = wstring_to_char(food[L"ct"]->AsString());

    iter        = (int) food[L"iter"]->AsNumber();
    key_size    = (int) food[L"ks"]->AsNumber();
    tag_size    = (int) food[L"ts"]->AsNumber();

    tag_size = (tag_size +7) / 8; // Make it bytes!
    key_size = (key_size +7) / 8; // Make it bytes

    // The actual cryptogram includes the tag size, so we need to take this into account later on!
    Datagram* cryptogram = BASE64::decode((unsigned char *) cyphertext, strlen(cyphertext));
    int cyphertext_data_length = cryptogram->length - tag_size;

    Datagram* salt = BASE64::decode((unsigned char *) salt_64, strlen(salt_64));
    Datagram* iv_raw = BASE64::decode((unsigned char *) iv_64, strlen(iv_64));
//    Datagram* aadata = BASE64::decode((const unsigned char *) "", 0); // Not sure if this is required since we don't use adata

    unsigned char* derived_key = (unsigned char*) malloc(sizeof(unsigned char) * key_size);

    // Assuming plaintext will always be smaller than the sjcl cyphertext which includes the tag and padding and stuff
    unsigned char* plaintext = (unsigned char*) std::calloc(cryptogram->length +1, sizeof(unsigned char));
    string s = string("The allocated string is: ") + string((char*)plaintext);

    /* PBKDF2 Key derivation with SHA256 as SJCL does by default */
    PKCS5_PBKDF2_HMAC(key.c_str(), key.length(), salt->data, salt->length, iter, EVP_sha256(), key_size, derived_key);

    char* ret = nullptr;
    // Decrypt the data
    int plaintext_len = decryptccm(cryptogram->data, cyphertext_data_length, (unsigned char *) adata, strlen(adata),
               &cryptogram->data[cyphertext_data_length], derived_key, iv_raw->data, plaintext);

    if (0 < plaintext_len) {
        Datagram* plaintext_base64 = BASE64::encode(plaintext, plaintext_len);
        ret = reinterpret_cast<char *>(plaintext_base64->data);

        /* do decoding now in the java part
         *
            // Try to make strings strings instead of json encoded strings
            JSONValue *result = JSON::Parse((char *) plaintext);
            if (result != NULL && result->IsString()) {
                ret =  wstring_to_char(result->AsString());
                free(plaintext);
                free(result);
            }
            else {
                ret = (char *) plaintext;
            }
        */
    }

    // Free up resources
    free(iv_64);
    free(adata);
    free(salt_64);
    free(cyphertext);
    free(data);
    free(derived_key);

    return ret;
}

int getInsecureRandomNumber(int min, int max) {
    srand(time(nullptr));
    return (rand() % (max - min + 1)) + min;
}

char* SJCL::encrypt(char* plaintext, const string& key) {
    int plaintext_len = strlen(plaintext);
    int iv_len = 13;    // use 13 because 15-lol (with initial lol=2) is hardcoded in the decryptccm implementation

    // strange iv_len calculation due to the decryptccm implementation
    if (plaintext_len >= 1<<16) iv_len--;
    if (plaintext_len >= 1<<24) iv_len--;

    int salt_len = getInsecureRandomNumber(12, 24);
    int iter = 1000;
    int key_size = 256;
    int tag_size = 64;
    int ciphertext_allocation_multiplicator = 3;    // give generated ciphertext some backup space

    int ks = (key_size +7) / 8;  // Make it bytes
    int ts = (tag_size +7) / 8;  // Make it bytes
    unsigned char *ciphertext;
    unsigned char *derived_key;
    unsigned char tag[ts];
    unsigned char *iv;
    unsigned char *salt;
    unsigned char *additional = (unsigned char *)"";
    char* ret = nullptr;

    iv = (unsigned char *) malloc(sizeof(unsigned char) * iv_len);
    salt = (unsigned char *) malloc(sizeof(unsigned char) * salt_len);
    derived_key = (unsigned char *) malloc(sizeof(unsigned char) * ks);

    RAND_bytes(iv, iv_len);
    RAND_bytes(salt, salt_len);

    // PBKDF2 Key derivation with SHA256 as SJCL does by default
    PKCS5_PBKDF2_HMAC(key.c_str(), key.length(), salt, salt_len, iter, EVP_sha256(), ks, derived_key);

    // Assuming ciphertext will not be bigger that the plaintext length * ciphertext_allocation_multiplicator
    ciphertext = (unsigned char *) malloc(sizeof(unsigned char) * strlen(plaintext) * ciphertext_allocation_multiplicator);

    unsigned char *tmp_plaintext = reinterpret_cast<unsigned char *>(plaintext);
    int ciphertext_len = encryptccm(tmp_plaintext, strlen(plaintext), additional, strlen ((char *)additional), derived_key, iv, iv_len, ciphertext, tag, ts);
    if (0 < ciphertext_len) {
        uint8_t *ciphertext_with_tag = static_cast<uint8_t *>(malloc(sizeof(char *) * (ciphertext_len + ts)));
        memcpy(ciphertext_with_tag, ciphertext, ciphertext_len);
        memcpy(ciphertext_with_tag + ciphertext_len, tag, ts);

        Datagram* ciphertext_base64 = BASE64::encode(
                reinterpret_cast<const unsigned char *>(ciphertext_with_tag), ciphertext_len + ts);
        char *tmpct64 = reinterpret_cast<char*>(ciphertext_base64->data);

        Datagram* salt_base64 = BASE64::encode(salt, salt_len);
        char *tmpsalt64 = reinterpret_cast<char*>(salt_base64->data);

        Datagram* iv_base64 = BASE64::encode(iv, iv_len);
        char *tmpiv64 = reinterpret_cast<char *>(iv_base64->data);


        nlohmann::json food;

        food["ct"] = tmpct64;
        food["salt"] = tmpsalt64;
        food["iv"] = tmpiv64;

        food["v"] = 1;
        food["iter"] = iter;
        food["ks"] = key_size;
        food["ts"] = tag_size;
        food["mode"] = "ccm";
        food["adata"] = "";
        food["cipher"] = "aes";

        string retrn = food.dump();
        ret = (char *) std::calloc(retrn.length() +1, sizeof(char));
        strcpy(ret, retrn.c_str());
    }

    // Free up resources
    free(ciphertext);
    free(iv);
    free(salt);
    free(derived_key);

    return ret;
}
