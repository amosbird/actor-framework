/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright (C) 2011 - 2017                                                  *
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

#include <string>
#include <condition_variable>

#include "caf/sec.hpp"
#include "caf/atom.hpp"
#include "caf/logger.hpp"
#include "caf/scheduler.hpp"
#include "caf/resumable.hpp"
#include "caf/actor_cast.hpp"
#include "caf/exit_reason.hpp"
#include "caf/local_actor.hpp"
#include "caf/actor_system.hpp"
#include "caf/actor_ostream.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/default_attachable.hpp"
#include "caf/binary_deserializer.hpp"

#include "caf/detail/private_thread.hpp"
#include "caf/detail/sync_request_bouncer.hpp"

namespace caf {

namespace {

constexpr auto mpol = mailbox_policy{};

} // namespace <anonymous>

local_actor::local_actor(actor_config& cfg)
    : monitorable_actor(cfg),
      mailbox_(mpol, mpol, mpol, mpol, mpol),
      context_(cfg.host),
      initial_behavior_fac_(std::move(cfg.init_fun)) {
  // nop
}

local_actor::~local_actor() {
  // nop
}

void local_actor::on_destroy() {
  CAF_PUSH_AID_FROM_PTR(this);
  if (!getf(is_cleaned_up_flag)) {
    on_exit();
    cleanup(exit_reason::unreachable, nullptr);
    monitorable_actor::on_destroy();
  }
}

void local_actor::request_response_timeout(const duration& d, message_id mid) {
  CAF_LOG_TRACE(CAF_ARG(d) << CAF_ARG(mid));
  if (!d.valid())
    return;
  system().scheduler().delayed_send(d, ctrl(), ctrl(), mid.response_id(),
                                    make_message(sec::request_timeout));
}

void local_actor::monitor(abstract_actor* ptr) {
  if (ptr != nullptr)
    ptr->attach(default_attachable::make_monitor(ptr->address(), address()));
}

void local_actor::demonitor(const actor_addr& whom) {
  CAF_LOG_TRACE(CAF_ARG(whom));
  auto ptr = actor_cast<strong_actor_ptr>(whom);
  if (ptr) {
    default_attachable::observe_token tk{address(), default_attachable::monitor};
    ptr->get()->detach(tk);
  }
}

void local_actor::on_exit() {
  // nop
}

message_id local_actor::new_request_id(message_priority mp) {
  auto result = ++last_request_id_;
  return mp == message_priority::normal ? result : result.with_high_priority();
}

void local_actor::send_exit(const actor_addr& whom, error reason) {
  send_exit(actor_cast<strong_actor_ptr>(whom), std::move(reason));
}

void local_actor::send_exit(const strong_actor_ptr& dest, error reason) {
  if (!dest)
    return;
  dest->get()->eq_impl(make_message_id(), nullptr, context(),
                       exit_msg{address(), std::move(reason)});
}

const char* local_actor::name() const {
  return "actor";
}

error local_actor::save_state(serializer&, const unsigned int) {
  CAF_RAISE_ERROR("local_actor::serialize called");
}

error local_actor::load_state(deserializer&, const unsigned int) {
  CAF_RAISE_ERROR("local_actor::deserialize called");
}

void local_actor::initialize() {
  // nop
}

bool local_actor::cleanup(error&& fail_state, execution_unit* host) {
  CAF_LOG_TRACE(CAF_ARG(fail_state));
  if (!mailbox_.closed()) {
    mailbox_.close();
    // TODO: messages that are stuck in the cache can get lost
    detail::sync_request_bouncer bounce{fail_state};
    while (mailbox_.queue().new_round(1000, bounce))
      ; // nop
  }
  // tell registry we're done
  unregister_from_system();
  monitorable_actor::cleanup(std::move(fail_state), host);
  CAF_LOG_TERMINATE_EVENT(this, fail_state);
  return true;
}

void local_actor::push_to_cache(mailbox_element_ptr ptr) {
  using namespace intrusive;
  auto& p = mailbox_.queue().policy();
  auto ts = p.task_size(*ptr);
  auto& qs = mailbox_.queue().queues();
  drr_cached_queue<mailbox_policy>* q;
  if (p.id_of(*ptr) == mailbox_policy::default_queue_index)
    q = &std::get<mailbox_policy::default_queue_index>(qs);
  else
    q = &std::get<mailbox_policy::high_priority_queue_index>(qs);
  q->inc_total_task_size(ts);
  q->cache().push_back(ptr.release());
}

} // namespace caf
