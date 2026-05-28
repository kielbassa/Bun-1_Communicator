#pragma once

#include "mbedtls/aes.h"
#include <string.h>
#include <StreamString.h>


// AES-256 key - can be updated at runtime via setAESKeyFromHex()
uint8_t aesKey[32] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
  0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F
};
// AES-256 - IV (Initialization Vector)
const uint8_t AES_IV[16] = {
  0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
  0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99
};

#define AES_MAX_LEN 112


String encryptAES(const String& plaintext) {
  if (plaintext.length() >= AES_MAX_LEN) {
    Serial.println("AES ERROR: message too long");
    return "";
  }

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, aesKey, 256);

  // Fixed size buffers instead of VLAs
  uint8_t input[AES_MAX_LEN]  = {0};
  uint8_t output[AES_MAX_LEN] = {0};

  size_t len    = plaintext.length();
  size_t padded = ((len / 16) + 1) * 16;

  memcpy(input, plaintext.c_str(), len);

  // PKCS#7 padding
  uint8_t padByte = padded - len;
  memset(input + len, padByte, padByte);

  uint8_t iv[16];
  memcpy(iv, AES_IV, 16);

  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, padded, iv, input, output);
  mbedtls_aes_free(&aes);

  // Convert to hex string
  String hex = "";
  hex.reserve(padded * 2);  // pre-allocate to avoid fragmentation
  for (size_t i = 0; i < padded; i++) {
    if (output[i] < 0x10) hex += "0";
    hex += String(output[i], HEX);
  }
  return hex;
}

// Update the AES-256 key from a 64-character hex string (case-insensitive).
// Returns true on success, false if the string length or characters are invalid.
bool setAESKeyFromHex(const String& hexKey) {
  if (hexKey.length() != 64) return false;
  uint8_t newKey[32];
  for (int i = 0; i < 32; i++) {
    String byteStr = hexKey.substring(i * 2, i * 2 + 2);
    char *end;
    long val = strtol(byteStr.c_str(), &end, 16);
    if (*end != '\0' || val < 0 || val > 255) return false;
    newKey[i] = (uint8_t)val;
  }
  memcpy(aesKey, newKey, 32);
  return true;
}

// Returns the current AES key as a 64-character lowercase hex string.
String getAESKeyHex() {
  String hex = "";
  hex.reserve(64);
  for (int i = 0; i < 32; i++) {
    if (aesKey[i] < 0x10) hex += "0";
    hex += String(aesKey[i], HEX);
  }
  return hex;
}

String decryptAES(const String& hexCipher){
    size_t len = hexCipher.length() / 2;

  if (len > AES_MAX_LEN || len % 16 != 0) {
    Serial.println("AES ERROR: invalid cipher length");
    return "";
  }

  // Fixed size buffers instead of VLAs
  uint8_t input[AES_MAX_LEN]  = {0};
  uint8_t output[AES_MAX_LEN] = {0};

  // Hex string to bytes
  for (size_t i = 0; i < len; i++) {
    input[i] = (uint8_t) strtol(
      hexCipher.substring(i * 2, i * 2 + 2).c_str(),
      nullptr, 16
    );
  }

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_dec(&aes, aesKey, 256);

  uint8_t iv[16];
  memcpy(iv, AES_IV, 16);

  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, len, iv, input, output);
  mbedtls_aes_free(&aes);

  // Strip PKCS#7 padding — validate before trusting the value.
  // An incorrect key produces garbage, which would cause padByte > len
  // and an unsigned underflow that crashes with a LoadStoreError.
  uint8_t padByte = output[len - 1];

  if (padByte == 0 || padByte > 16 || padByte > len) {
    Serial.println("AES ERROR: invalid padding (wrong key?)");
    return "";
  }
  size_t realLen = len - padByte;

  String result = "";
  result.reserve(realLen);
  for (size_t i = 0; i < realLen; i++) {
    result += (char)output[i];
  }
  return result;
}
