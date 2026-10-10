// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What a generated C++ client uses of libember (github.com/da0x/libember), declared
// and nothing more, so the compiler's tests can check a generated header compiles
// without libember. With EMBER_INCLUDE naming libember's include folder, the tests
// build against libember itself instead.
#pragma once
#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace ember {
struct options { std::string project, api_key, firestore_host, auth_host; static options for_project(std::string p) { return {p, "", "", ""}; } };
class value {
public:
    bool is_null() const { return true; } bool is_bool() const { return false; } bool is_integer() const { return false; }
    bool is_double() const { return false; } bool is_string() const { return false; } bool is_time() const { return false; }
    bool is_array() const { return false; } bool is_map() const { return false; }
    bool as_bool() const { return false; } long long as_integer() const { return 0; } double as_double() const { return 0; }
    const std::string& as_string() const { static std::string s; return s; }
    const std::vector<value>& as_array() const { static std::vector<value> a; return a; }
    const std::map<std::string, value>& as_map() const { static std::map<std::string, value> m; return m; }
    std::chrono::sys_time<std::chrono::nanoseconds> as_time() const { return {}; }
    const value& operator[](std::string_view) const { return *this; }
};
value parse_json(std::string_view);
struct snapshot { std::string path; bool exists; bool from_cache; value data; };
struct error { int code; std::string message; };
struct user { std::string uid; std::map<std::string, value> claims; };
class auth { public: explicit auth(options); void sign_in_with_custom_token(const std::string&); void sign_in_with_idp(const std::string&, const std::string&);
  void sign_in_with_refresh_token(const std::string&); void sign_out(); std::optional<user> current_user() const; std::string id_token(); std::string refresh_token() const; };
struct failure : std::runtime_error { int status; using std::runtime_error::runtime_error; };
struct response { int status; std::string body; };
response https_post(const std::string&, const std::vector<std::pair<std::string, std::string>>&, const std::string&);
response https_get(const std::string&, const std::vector<std::pair<std::string, std::string>>&);
class executor { public: virtual ~executor() = default; virtual void post(std::function<void()>) = 0; };
class database { public: database(options, auth* = nullptr, executor* = nullptr); ~database();
  snapshot get(const std::string&);
  class registration { public: void remove(); ~registration(); };
  registration listen(const std::string&, std::function<void(const snapshot&)>, std::function<void(const error&)> = {}); };
}
