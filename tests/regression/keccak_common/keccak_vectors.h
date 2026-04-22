#ifndef _KECCAK_VECTORS_H_
#define _KECCAK_VECTORS_H_

#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace keccak_test {

constexpr uint32_t kKeccakDigestBytes = 32;
constexpr const char* kDefaultByteShortRsp = "/home/jiangbowang/aphd2026/Keccak/sha-3bytetestvectors/SHA3_256ShortMsg.rsp";
constexpr const char* kDefaultByteLongRsp  = "/home/jiangbowang/aphd2026/Keccak/sha-3bytetestvectors/SHA3_256LongMsg.rsp";
constexpr const char* kDefaultBitShortRsp  = "/home/jiangbowang/aphd2026/Keccak/sha-3bittestvectors/SHA3_256ShortMsg.rsp";
constexpr const char* kDefaultBitLongRsp   = "/home/jiangbowang/aphd2026/Keccak/sha-3bittestvectors/SHA3_256LongMsg.rsp";

struct KeccakVector {
  uint64_t bit_len;
  std::vector<uint8_t> msg;
  std::array<uint8_t, kKeccakDigestBytes> digest;
  std::string source;
};

inline uint32_t bytes_for_bits(uint64_t bit_len) {
  return static_cast<uint32_t>((bit_len + 7u) / 8u);
}

inline std::string trim(const std::string& value) {
  size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
    ++begin;
  }
  size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(begin, end - begin);
}

inline int hex_nibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (c >= 'a' && c <= 'f') {
    return 10 + (c - 'a');
  }
  throw std::runtime_error("invalid hex digit");
}

inline std::vector<uint8_t> parse_hex_bytes(const std::string& hex) {
  std::string clean = trim(hex);
  if (clean.empty()) {
    return {};
  }
  if ((clean.size() & 1u) != 0u) {
    throw std::runtime_error("hex string must have even length");
  }
  std::vector<uint8_t> out(clean.size() / 2u);
  for (size_t i = 0; i < out.size(); ++i) {
    out[i] = static_cast<uint8_t>((hex_nibble(clean[2 * i]) << 4) | hex_nibble(clean[2 * i + 1]));
  }
  return out;
}

inline std::array<uint8_t, kKeccakDigestBytes> parse_digest(const std::string& hex) {
  auto bytes = parse_hex_bytes(hex);
  if (bytes.size() != kKeccakDigestBytes) {
    throw std::runtime_error("unexpected digest length");
  }
  std::array<uint8_t, kKeccakDigestBytes> out {};
  for (size_t i = 0; i < out.size(); ++i) {
    out[i] = bytes[i];
  }
  return out;
}

inline std::vector<KeccakVector> load_rsp_vectors(const std::string& path, uint32_t limit = 0) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("failed to open RSP file: " + path);
  }

  std::vector<KeccakVector> vectors;
  uint64_t bit_len = 0;
  std::vector<uint8_t> msg;
  bool have_len = false;
  bool have_msg = false;
  bool sha3_256_section = false;

  std::string line;
  while (std::getline(input, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    if (line[0] == '[') {
      sha3_256_section = (line == "[L = 256]");
      continue;
    }
    if (!sha3_256_section) {
      continue;
    }
    if (line.rfind("Len =", 0) == 0) {
      bit_len = std::stoull(trim(line.substr(5)));
      have_len = true;
      have_msg = false;
      continue;
    }
    if (line.rfind("Msg =", 0) == 0) {
      msg = parse_hex_bytes(line.substr(5));
      if (msg.size() < bytes_for_bits(bit_len)) {
        throw std::runtime_error("message too short for length in " + path);
      }
      have_msg = true;
      continue;
    }
    if (line.rfind("MD =", 0) == 0 && have_len && have_msg) {
      vectors.push_back({bit_len, msg, parse_digest(line.substr(4)), path});
      if (limit != 0 && vectors.size() >= limit) {
        break;
      }
      have_len = false;
      have_msg = false;
    }
  }

  return vectors;
}

inline uint32_t digest_checksum(const std::vector<uint8_t>& digest) {
  uint32_t checksum = 0x6a09e667u;
  for (uint8_t byte : digest) {
    checksum = (checksum << 5) ^ (checksum >> 2) ^ byte;
  }
  return checksum;
}

inline const char* default_rsp_for_kind(const std::string& kind) {
  if (kind == "byte-short") {
    return kDefaultByteShortRsp;
  }
  if (kind == "byte-long") {
    return kDefaultByteLongRsp;
  }
  if (kind == "bit-short") {
    return kDefaultBitShortRsp;
  }
  if (kind == "bit-long") {
    return kDefaultBitLongRsp;
  }
  throw std::runtime_error("unsupported dataset kind: " + kind);
}

} // namespace keccak_test

#endif
