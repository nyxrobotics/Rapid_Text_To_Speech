//  MeCab -- Yet Another Part-of-Speech and Morphological Analyzer
//
//
//  Copyright(C) 2001-2006 Taku Kudo <taku@chasen.org>
//  Copyright(C) 2004-2006 Nippon Telegraph and Telephone Corporation

/* ----------------------------------------------------------------- */
/*           The Japanese TTS System "Open JTalk"                    */
/*           developed by HTS Working Group                          */
/*           http://open-jtalk.sourceforge.net/                      */
/* ----------------------------------------------------------------- */
/*                                                                   */
/*  Copyright (c) 2008-2011  Nagoya Institute of Technology          */
/*                           Department of Computer Science          */
/*                                                                   */
/* All rights reserved.                                              */
/*                                                                   */
/* Redistribution and use in source and binary forms, with or        */
/* without modification, are permitted provided that the following   */
/* conditions are met:                                               */
/*                                                                   */
/* - Redistributions of source code must retain the above copyright  */
/*   notice, this list of conditions and the following disclaimer.   */
/* - Redistributions in binary form must reproduce the above         */
/*   copyright notice, this list of conditions and the following     */
/*   disclaimer in the documentation and/or other materials provided */
/*   with the distribution.                                          */
/* - Neither the name of the HTS working group nor the names of its  */
/*   contributors may be used to endorse or promote products derived */
/*   from this software without specific prior written permission.   */
/*                                                                   */
/* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND            */
/* CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,       */
/* INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF          */
/* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE          */
/* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS */
/* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,          */
/* EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED   */
/* TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,     */
/* DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON */
/* ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,   */
/* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY    */
/* OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE           */
/* POSSIBILITY OF SUCH DAMAGE.                                       */
/* ----------------------------------------------------------------- */

#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <iterator> /* for Open JTalk */

#include "utils.h"
#include "common.h"
#include "dictionary_rewriter.h"
#include "iconv_utils.h"

namespace {

using namespace MeCab;

void append_rewrite_rule(RewriteRules *r, char* str) {
  char *col[3];
  const size_t n = tokenize2(str, " \t", col, 3);
  CHECK_DIE(n >= 2) << "format error: " << str;
  r->resize(r->size() + 1);
  std::string tmp;
  if (n >= 3) {
    tmp = col[1];
    tmp += ' ';
    tmp += col[2];
    col[1] = const_cast<char *>(tmp.c_str());
  }
  r->back().set_pattern(col[0], col[1]);
}

bool match_rewrite_pattern(const char *pat,
                           const char *str) {
  if (pat[0] == '*' || std::strcmp(pat, str) == 0)
    return true;

  size_t len = std::strlen(pat);
  if (len >= 3 && pat[0] == '(' && pat[len-1] == ')') {
    char buf[BUF_SIZE];
    char *col[BUF_SIZE];
    CHECK_DIE(len < sizeof(buf) - 3) << "too long parameter";
    std::strncpy(buf, pat + 1, BUF_SIZE);
    buf[len-2] = '\0';
    const size_t n = tokenize(buf, "|", col, sizeof(col));
    CHECK_DIE(n < sizeof(col)) << "too long OR nodes";
    for (size_t i = 0; i < n; ++i) {
      if (std::strcmp(str, col[i]) == 0) return true;
    }
  }
  return false;
}
}

namespace MeCab {

bool RewritePattern::set_pattern(const char *src,
                                 const char *dst) {
  char buf[BUF_SIZE];
  spat_.clear();
  dpat_.clear();

  std::strncpy(buf, src, sizeof(buf));
  tokenizeCSV(buf, back_inserter(spat_), 512);

  std::strncpy(buf, dst, sizeof(buf));
  tokenizeCSV(buf, back_inserter(dpat_), 512);

  return (spat_.size() && dpat_.size());
}

bool RewritePattern::rewrite(size_t size,
                             const char **input,
                             std::string *output) const {
  if (spat_.size() > size) return false;
  for (size_t i = 0; i < spat_.size(); ++i) {
    if (!match_rewrite_pattern(spat_[i].c_str(), input[i]))
      return false;
  }

  output->clear();
  for (size_t i = 0; i < dpat_.size(); ++i) {
    std::string elm;
    const char *begin = dpat_[i].c_str();
    const char *end = begin + dpat_[i].size();
    for (const char *p = begin; p < end; ++p) {
      if (*p == '$') {
        ++p;
        size_t n = 0;
        for (; p < end; ++p) {
          switch (*p) {
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
              n = 10 * n + (*p - '0');
              break;
            default:
              goto NEXT;
          }
        }
     NEXT:
        CHECK_DIE(n > 0 && (n - 1) < size)
            << " out of range: [" << dpat_[i] << "] " << n;
        elm += input[n - 1];
        if (p < end) elm += *p;
      } else {
        elm += *p;
      }
    }
    CHECK_DIE(escape_csv_element(&elm));
    *output += elm;
    if (i + 1 != dpat_.size()) *output += ",";
  }

  return true;
}

bool RewriteRules::rewrite(size_t size,
                           const char **input,
                           std::string *output) const {
  for (size_t i = 0; i < this->size(); ++i) {
    if ((*this)[i].rewrite(size, input, output))
      return true;
  }
  return false;
}

void DictionaryRewriter::clear() { cache_.clear(); }

bool DictionaryRewriter::open(const char *filename,
                              Iconv *iconv) {
  std::ifstream ifs(filename);
  CHECK_DIE(ifs) << "no such file or directory: " << filename;
  int append_to = 0;
  std::string line;
  while (std::getline(ifs, line)) {
    if (iconv) iconv->convert(&line);
    if (line.empty() || line[0] == '#') continue;
    if (line == "[unigram rewrite]") {
      append_to = 1;
    } else if (line == "[left rewrite]") {
      append_to = 2;
    } else if (line == "[right rewrite]") {
      append_to = 3;
    } else {
      CHECK_DIE(append_to != 0) << "no sections found";
      char *str = const_cast<char *>(line.c_str());
      switch (append_to) {
        case 1: append_rewrite_rule(&unigram_rewrite_, str); break;
        case 2: append_rewrite_rule(&left_rewrite_,    str); break;
        case 3: append_rewrite_rule(&right_rewrite_,   str); break;
      }
    }
  }
  return true;
}

// without cache
bool DictionaryRewriter::rewrite(const std::string &feature,
                                 std::string *ufeature,
                                 std::string *lfeature,
                                 std::string *rfeature) const {
  char buf[BUF_SIZE];
  char *col[BUF_SIZE];
  CHECK_DIE(feature.size() < sizeof(buf) - 1) << "too long feature";
  std::strncpy(buf, feature.c_str(), sizeof(buf) - 1);
  size_t n = tokenizeCSV(buf, col, sizeof(col));
  CHECK_DIE(n < sizeof(col)) << "too long CSV entities";
  return (unigram_rewrite_.rewrite(n, const_cast<const char **>(col),
                                   ufeature) &&
          left_rewrite_.rewrite(n, const_cast<const char **>(col),
                                lfeature) &&
          right_rewrite_.rewrite(n, const_cast<const char **>(col),
                                 rfeature));
}

// with cache
bool DictionaryRewriter::rewrite2(const std::string &feature,
                                  std::string *ufeature,
                                  std::string *lfeature,
                                  std::string *rfeature) {
  std::map<std::string, FeatureSet>::const_iterator it = cache_.find(feature);
  if (it == cache_.end()) {
    if (!rewrite(feature, ufeature, lfeature, rfeature)) return false;
    FeatureSet f;
    f.ufeature = *ufeature;
    f.lfeature = *lfeature;
    f.rfeature = *rfeature;
    cache_.insert(std::make_pair<std::string, FeatureSet>(feature, f));
  } else {
    *ufeature = it->second.ufeature;
    *lfeature = it->second.lfeature;
    *rfeature = it->second.rfeature;
  }

  return true;
}

bool POSIDGenerator::open(const char *filename,
                          Iconv *iconv) {
  std::ifstream ifs(filename);
  if (!ifs) {
    std::cerr << filename
              << " is not found. minimum setting is used" << std::endl;
    rewrite_.resize(1);
    rewrite_.back().set_pattern("*", "1");
    return true;
  }

  std::string line;
  char *col[2];
  while (std::getline(ifs, line)) {
    if (iconv) iconv->convert(&line);
    const size_t n = tokenize2(const_cast<char *>(line.c_str()),
                               " \t", col, 2);
    CHECK_DIE(n == 2) << "format error: " << line;
    for (char *p = col[1]; *p; ++p) {
      CHECK_DIE(*p >= '0' && *p <= '9') << "not a number: " << col[1];
    }
    rewrite_.resize(rewrite_.size() + 1);
    rewrite_.back().set_pattern(col[0], col[1]);
  }
  return true;
}

int POSIDGenerator::id(const char *feature) const {
  char buf[BUF_SIZE];
  char *col[BUF_SIZE];
  CHECK_DIE(std::strlen(feature) < sizeof(buf) - 1) << "too long feature";
  std::strncpy(buf, feature, sizeof(buf) - 1);
  const size_t n = tokenizeCSV(buf, col, sizeof(col));
  CHECK_DIE(n < sizeof(col)) << "too long CSV entities";
  std::string tmp;
  if (!rewrite_.rewrite(n, const_cast<const char **>(col), &tmp))
    return -1;
  return std::atoi(tmp.c_str());
}
}
