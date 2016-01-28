/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright (C) 2011 - 2015                                                  *
 * Dominik Charousset <dominik.charousset (at) haw-hamburg.de>                *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#include "caf/config.hpp"

#define CAF_SUITE function_view
#include "caf/test/unit_test.hpp"

#include <string>
#include <vector>

#include "caf/all.hpp"

using namespace caf;

namespace {

using calculator = typed_actor<replies_to<int, int>::with<int>>;

calculator::behavior_type adder() {
  return {
    [](int x, int y) {
      return x + y;
    }
  };
}

calculator::behavior_type multiplier() {
  return {
    [](int x, int y) {
      return x * y;
    }
  };
}

calculator::behavior_type divider() {
  return {
    [](int x, int y) -> maybe<int> {
      if (y == 0)
        return none;
      return x / y;
    }
  };
}

using doubler = typed_actor<replies_to<int>::with<int, int>>;

doubler::behavior_type simple_doubler() {
  return {
    [](int x) {
      return std::make_tuple(x, x);
    }
  };
}

struct fixture {
  actor_system system;
  std::vector<actor_addr> testees;

  template <class F>
  typename infer_handle_from_fun<F>::type spawn(F fun) {
    auto result = system.spawn(fun);
    testees.emplace_back(result.address());
    return result;
  }

  ~fixture() {
    for (auto& testee : testees)
      anon_send_exit(testee, exit_reason::kill);
  }
};

} // namespace <anonymous>

CAF_TEST_FIXTURE_SCOPE(function_view_tests, fixture)

CAF_TEST(empty_function_fiew) {
  function_view<calculator> f;
  try {
    f(10, 20);
    CAF_ERROR("line must be unreachable");
  }
  catch (std::bad_function_call&) {
    CAF_CHECK(true);
  }
}

CAF_TEST(single_res_function_view) {
  auto f = make_function_view(spawn(adder));
  CAF_CHECK(f(3, 4) == 7);
  CAF_CHECK(f != nullptr);
  CAF_CHECK(nullptr != f);
  function_view<calculator> g;
  g = std::move(f);
  CAF_CHECK(f == nullptr);
  CAF_CHECK(nullptr == f);
  CAF_CHECK(g != nullptr);
  CAF_CHECK(nullptr != g);
  CAF_CHECK(g(10, 20) == 30);
  g.assign(spawn(multiplier));
  CAF_CHECK(g(10, 20) == 200);
  g.assign(spawn(divider));
  try {
    g(1, 0);
    CAF_ERROR("expected exception");
  }
  catch (actor_exited&) {
    CAF_CHECK(true);
  }
  CAF_CHECK(g == nullptr);
  g.assign(spawn(divider));
  CAF_CHECK(g(4, 2) == 2);
}

CAF_TEST(tuple_res_function_view) {
  auto f = make_function_view(spawn(simple_doubler));
  CAF_CHECK(f(10) == std::make_tuple(10, 10));
}

CAF_TEST_FIXTURE_SCOPE_END()
