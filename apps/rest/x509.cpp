/**
 * @brief Wrappers on top of OpenSSL methods in order to deal with x509 certificates
 *
 * Adapted from: https://gist.github.com/nathan-osman/5041136
 */
#pragma once

#include <cstdio>
#include <memory>

#include <openssl/pem.h>
#include <openssl/x509.h>

#include <boost/filesystem.hpp>

#include <helpers/logger.cpp>

namespace x509 {

/**
 * @brief Generates a 2048-bit RSA key.
 *
 * @return safe_ptr<EVP_PKEY>: a safe unique_ptr to a private key
 */
EVP_PKEY *generate_key() {
  auto pkey = EVP_PKEY_new();
  if (!pkey) {
    logs::log(logs::error, "Unable to create EVP_PKEY structure.");
    return nullptr;
  }

  auto big_num = BN_new();
  auto rsa = RSA_new();
  BN_set_word(big_num, RSA_F4);
  RSA_generate_key_ex(rsa, 2048, big_num, nullptr);
  if (!EVP_PKEY_assign_RSA(pkey, rsa)) {
    logs::log(logs::error, "Unable to generate 2048-bit RSA key.");
    EVP_PKEY_free(pkey);
    return nullptr;
  }

  return pkey;
}

/**
 * @brief Generates a self-signed x509 certificate.
 *
 * @param pkey: a private key generated with generate_key()
 * @return X509*: a pointer to a x509 certificate
 */
X509 *generate_x509(EVP_PKEY *pkey) {
  /* Allocate memory for the X509 structure. */
  X509 *x509 = X509_new();
  if (!x509) {
    logs::log(logs::error, "Unable to create X509 structure.");
    return nullptr;
  }

  /* Set the serial number. */
  ASN1_INTEGER_set(X509_get_serialNumber(x509), 1); // Set the serial number.
  X509_set_version(x509, 2);

  auto valid_years = 630720000L; // This certificate is valid for 20 years
  X509_gmtime_adj(X509_get_notBefore(x509), 0);
  X509_gmtime_adj(X509_get_notAfter(x509), valid_years);

  /* Set the public key for our certificate. */
  X509_set_pubkey(x509, pkey);

  /* We want to copy the subject name to the issuer name. */
  X509_NAME *name = X509_get_subject_name(x509);

  /* Set the country code and common name. */
  X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (unsigned char *)"IT", -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, (unsigned char *)"GamesOnWhales", -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char *)"localhost", -1, -1, 0);
  X509_set_issuer_name(x509, name);

  /* Actually sign the certificate with our key. */
  if (!X509_sign(x509, pkey, EVP_sha256())) {
    logs::log(logs::error, "Error signing certificate.");
    X509_free(x509);
    return nullptr;
  }

  return x509;
}

/**
 * @brief Reads a X509 certificate string
 */
X509 *cert_from_string(const std::string &cert) {
  BIO *bio;
  X509 *certificate;

  bio = BIO_new(BIO_s_mem());
  BIO_puts(bio, cert.c_str());
  certificate = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);

  BIO_free(bio);
  return certificate;
}

/**
 * @brief Reads a X509 certificate from file
 */
X509 *cert_from_file(const std::string &cert_path) {
  X509 *certificate;
  BIO *bio;

  bio = BIO_new(BIO_s_file());
  if (BIO_read_filename(bio, cert_path.c_str()) <= 0) {
    logs::log(logs::error, "Error reading certificate: {}.", cert_path);
    return nullptr;
  }
  certificate = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);

  BIO_free(bio);
  return certificate;
}

/**
 * @brief Reads a private key from file
 */
EVP_PKEY *pkey_from_file(const std::string &pkey_path) {
  EVP_PKEY *pkey;
  BIO *bio;

  bio = BIO_new(BIO_s_file());
  if (BIO_read_filename(bio, pkey_path.c_str()) <= 0) {
    logs::log(logs::error, "Error reading certificate: {}.", pkey_path);
    return nullptr;
  }
  pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);

  BIO_free(bio);
  return pkey;
}

/**
 * @brief Write cert and key to disk
 *
 * @param pkey: a private key generated with generate_key()
 * @param pkey_filename: the name of the key file to be saved
 * @param x509: a certificate generated with generate_x509()
 * @param cert_filename: the name of the cert file to be saved
 * @return true when both pkey and x509 are stored on disk
 * @return false when one or both failed
 */
bool write_to_disk(EVP_PKEY *pkey, const std::string &pkey_filename, X509 *x509, const std::string &cert_filename) {
  /* Open the PEM file for writing the key to disk. */
  FILE *pkey_file = fopen(pkey_filename.c_str(), "wb");
  if (!pkey_file) {
    logs::log(logs::error, "Unable to open {} for writing.", pkey_filename);
    return false;
  }

  /* Write the key to disk. */
  bool ret = PEM_write_PrivateKey(pkey_file, pkey, nullptr, nullptr, 0, nullptr, nullptr);
  fclose(pkey_file);

  if (!ret) {
    logs::log(logs::error, "Unable to write {} to disk.", pkey_filename);
    return false;
  }

  /* Open the PEM file for writing the certificate to disk. */
  FILE *x509_file = fopen(cert_filename.c_str(), "wb");
  if (!x509_file) {
    logs::log(logs::error, "Unable to open {} for writing", cert_filename);
    return false;
  }

  /* Write the certificate to disk. */
  ret = PEM_write_X509(x509_file, x509);
  fclose(x509_file);

  if (!ret) {
    logs::log(logs::error, "Unable to write {} to disk.", cert_filename);
    return false;
  }

  return true;
}

/**
 * @param pkey_filename: the name of the key file to be saved
 * @param cert_filename: the name of the cert file to be saved
 * @return true when both files are present
 */
bool cert_exists(const std::string &pkey_filename, const std::string &cert_filename) {
  return boost::filesystem::exists(pkey_filename) && boost::filesystem::exists(cert_filename);
}

/**
 * @return the certificate signature
 */
std::string get_cert_signature(const X509 *cert) {
  const ASN1_BIT_STRING *asn1 = nullptr;
  X509_get0_signature(&asn1, nullptr, cert);

  return {(const char *)asn1->data, (std::size_t)asn1->length};
}

std::string get_key_content(EVP_PKEY *pkey, bool private_key) {
  BIO *bio;
  BUF_MEM *bufmem;
  char *pem;

  bio = BIO_new(BIO_s_mem());

  if (private_key) {
    PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
  } else {
    PEM_write_bio_PUBKEY(bio, pkey);
  }

  const int keylen = BIO_pending(bio);
  char *key = (char *)calloc(keylen + 1, 1);
  BIO_read(bio, key, keylen);
  BIO_free_all(bio);

  return {key, static_cast<size_t>(keylen)};
}

/**
 * @return the private key content
 */
std::string get_pkey_content(EVP_PKEY *pkey) {
  return get_key_content(pkey, true);
}

/**
 *
 * @return the certificate public key content
 */
std::string get_cert_public_key(X509 *cert) {
  auto pkey = X509_get_pubkey(cert);
  return get_key_content(pkey, false);
}

/**
 * @brief: cleanup pointers after use
 *
 * TODO: replace with smart pointers?
 *
 * @param pkey: a private key generated with generate_key()
 * @param x509: a certificate generated with generate_x509()
 */
void cleanup(EVP_PKEY *pkey, X509 *cert) {
  EVP_PKEY_free(pkey);
  X509_free(cert);
}

} // namespace x509