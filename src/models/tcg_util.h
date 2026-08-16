#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

class TcgUtil {
public:
#ifdef _WIN32
  // Convert a UTF-8
  static std::wstring utf8_to_wide(const std::string &s) {
    if (s.empty())
      return std::wstring();
    int n =
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
  }
#endif

  static std::vector<std::string>
  read_json_string_array(const std::string &path) {
#ifdef _WIN32
    std::ifstream f(utf8_to_wide(path));
#else
    std::ifstream f(path);
#endif
    if (!f)
      throw std::runtime_error("cannot open " + path);
    std::string s((std::istreambuf_iterator<char>(f)),
                  std::istreambuf_iterator<char>());
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
      if (s[i] == '"') {
        std::string cur;
        for (++i; i < s.size() && s[i] != '"'; ++i) {
          if (s[i] == '\\' && i + 1 < s.size())
            ++i; // skip escape
          cur.push_back(s[i]);
        }
        out.push_back(cur);
      }
      ++i;
    }
    return out;
  }

  static std::vector<int> read_npy_int(const std::string &path) {
#ifdef _WIN32
    std::ifstream f(utf8_to_wide(path), std::ios::binary);
#else
    std::ifstream f(path, std::ios::binary);
#endif
    if (!f)
      throw std::runtime_error("cannot open " + path);
    char magic[6];
    f.read(magic, 6); // \x93NUMPY
    unsigned char ver[2];
    f.read((char *)ver, 2);
    uint16_t hlen;
    f.read((char *)&hlen, 2);
    std::string header(hlen, '\0');
    f.read(&header[0], hlen);
    bool is64 = header.find("<i8") != std::string::npos ||
                header.find("|i8") != std::string::npos;
    bool is32 = header.find("<i4") != std::string::npos;
    // element count = file remainder / element size
    std::streampos data_start = f.tellg();
    f.seekg(0, std::ios::end);
    std::streamoff bytes = f.tellg() - data_start;
    f.seekg(data_start);
    std::vector<int> out;
    if (is64) {
      size_t n = bytes / 8;
      out.resize(n);
      for (size_t i = 0; i < n; ++i) {
        int64_t v;
        f.read((char *)&v, 8);
        out[i] = (int)v;
      }
    } else if (is32) {
      size_t n = bytes / 4;
      out.resize(n);
      for (size_t i = 0; i < n; ++i) {
        int32_t v;
        f.read((char *)&v, 4);
        out[i] = (int)v;
      }
    } else {
      throw std::runtime_error(
          "row2card.npy: unexpected dtype (need <i8 or <i4)");
    }
    return out;
  }

  static void write_npy_int(const std::string &path,
                            const std::vector<int> &data) {
#ifdef _WIN32
    std::ofstream f(path, std::ios::binary);
#else
    std::ofstream f(path, std::ios::binary);
#endif
    if (!f)
      throw std::runtime_error("cannot open " + path + " for writing");

    std::string dict = "{'descr': '<i4', 'fortran_order': False, 'shape': (" +
                       std::to_string(data.size()) + ",), }";
    int prefix_len = 10;
    int total_hdr_len = prefix_len + (int)dict.size() + 1;
    int pad = (16 - (total_hdr_len % 16)) % 16;
    dict.append(pad, ' ');
    dict.push_back('\n');
    uint16_t hlen = static_cast<uint16_t>(dict.size());

    f.write("\x93NUMPY\x01\x00", 8);
    f.write(reinterpret_cast<const char *>(&hlen), 2);
    f.write(dict.data(), dict.size());
    f.write(reinterpret_cast<const char *>(data.data()),
            data.size() * sizeof(int));
  }

  static void write_json_string_array(const std::string &path,
                                      const std::vector<std::string> &vec) {
    std::ofstream f(path);
    if (!f)
      throw std::runtime_error("cannot open " + path + " for writing");

    f << "[\n";
    for (size_t i = 0; i < vec.size(); ++i) {
      f << "  \"" << vec[i] << "\"";
      if (i + 1 < vec.size())
        f << ",";
      f << "\n";
    }
    f << "]\n";
  }
};