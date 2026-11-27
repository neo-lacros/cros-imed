// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_IME_SELFTEST_REGISTRY_H_
#define CROS_IME_SELFTEST_REGISTRY_H_

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace cros_ime {
namespace selftest {

class Checker {
 public:
  void Section(const std::string& name) {
    if (!section_.empty() && section_checks_ == 0) {
      std::cout << section_ << ": skipped\n";
    }
    section_ = name;
    header_printed_ = false;
    section_checks_ = 0;
  }

  void Ok(const std::string& what) {
    EnsureHeader();
    ++section_checks_;
    ++passed_;
    std::cout << "[ ok ] " << what << '\n';
  }

  bool Expect(const std::string& what,
              bool condition,
              const std::string& detail = "") {
    EnsureHeader();
    ++section_checks_;
    if (condition) {
      ++passed_;
      std::cout << "[ ok ] " << what << '\n';
      return true;
    }
    ++failed_;
    std::cout << "[FAIL] " << what;
    if (!detail.empty()) {
      std::cout << ": " << detail;
    }
    std::cout << '\n';
    failures_.push_back(detail.empty() ? what : what + ": " + detail);
    return false;
  }

  bool ExpectStr(const std::string& what,
                 const std::string& got,
                 const std::string& want) {
    return Expect(what, got == want,
                  "expected \"" + want + "\" got \"" + got + "\"");
  }

  bool ExpectStrEq(const std::string& what,
                   const char* got,
                   const std::string& want) {
    const std::string actual = got != nullptr ? got : "(null)";
    return ExpectStr(what, actual, want);
  }

  bool ExpectInt(const std::string& what, long long got, long long want) {
    return Expect(what, got == want,
                  "expected " + std::to_string(want) + " got " +
                      std::to_string(got));
  }

  bool ExpectContains(const std::string& what,
                      const std::string& haystack,
                      const std::string& needle) {
    return Expect(what, haystack.find(needle) != std::string::npos,
                  "expected to contain \"" + needle + "\" in \"" + haystack +
                      "\"");
  }

  bool ExpectNotContains(const std::string& what,
                         const std::string& haystack,
                         const std::string& needle) {
    return Expect(what, haystack.find(needle) == std::string::npos,
                  "expected not to contain \"" + needle + "\" in \"" +
                      haystack + "\"");
  }

  bool ExpectGe(const std::string& what, long long got, long long want) {
    return Expect(what, got >= want,
                  "expected >= " + std::to_string(want) + " got " +
                      std::to_string(got));
  }

  bool ExpectLe(const std::string& what, long long got, long long want) {
    return Expect(what, got <= want,
                  "expected <= " + std::to_string(want) + " got " +
                      std::to_string(got));
  }

  void Finish() {
    if (!section_.empty() && section_checks_ == 0) {
      std::cout << section_ << ": skipped\n";
    }
  }

  int passed() const { return passed_; }
  int failed() const { return failed_; }
  const std::vector<std::string>& failures() const { return failures_; }

 private:
  void EnsureHeader() {
    if (!header_printed_ && !section_.empty()) {
      std::cout << "== " << section_ << " ==\n";
      header_printed_ = true;
    }
  }

  std::string section_;
  bool header_printed_ = false;
  int section_checks_ = 0;
  int passed_ = 0;
  int failed_ = 0;
  std::vector<std::string> failures_;
};

using Runner = void (*)(Checker&);

struct Registration {
  std::string name;
  int order;
  Runner fn;
};

inline std::vector<Registration>& RegistryStorage() {
  static std::vector<Registration> registrations;
  return registrations;
}

inline void Register(const std::string& name, int order, Runner runner) {
  RegistryStorage().push_back(Registration{name, order, runner});
}

inline std::vector<Registration> Registrations() {
  std::vector<Registration> sorted = RegistryStorage();
  std::sort(sorted.begin(), sorted.end(),
            [](const Registration& left, const Registration& right) {
              if (left.order != right.order) {
                return left.order < right.order;
              }
              return left.name < right.name;
            });
  return sorted;
}

struct AutoRegister {
  AutoRegister(const std::string& name, int order, Runner runner) {
    Register(name, order, runner);
  }
};

inline std::string& BuildRootStorage() {
  static std::string build_root = "./build";
  return build_root;
}

inline void SetBuildRoot(const std::string& directory) {
  BuildRootStorage() = directory.empty() ? "./build" : directory;
}

inline const std::string& GetBuildRoot() {
  return BuildRootStorage();
}

}  // namespace selftest
}  // namespace cros_ime

#define CROS_IME_SELFTEST(section_name, order, fn_name)                 \
  static void fn_name(::cros_ime::selftest::Checker& checker);          \
  static ::cros_ime::selftest::AutoRegister fn_name##_registration(     \
      section_name, order, fn_name);                                    \
  static void fn_name(::cros_ime::selftest::Checker& checker)

#endif  // CROS_IME_SELFTEST_REGISTRY_H_
