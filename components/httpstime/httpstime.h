//
// Copyright (c) 2026 Tamas Tevesz <ice@extreme.hu>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//

// Copilot helped write this code.

#pragma once

#include <string>

#include "esphome/core/component.h"
#include "esphome/components/http_request/http_request.h"
#include "esphome/components/time/real_time_clock.h"

namespace esphome {
namespace httpstime {

class HTTPSTimeComponent : public time::RealTimeClock, public Parented<http_request::HttpRequestComponent> {
 public:
  void set_url(const std::string &url) { url_ = url; }

  void call_setup() override;
  void update() override;
  void dump_config() override;

 protected:
  std::string url_;

  bool steady_interval_captured_{false};
  uint32_t steady_interval_ms_{0};

  enum class AttemptResult : uint8_t {
    NET_FAIL,
    NO_TIME,
    TIME_SET,
  };

  enum class SyncState : uint8_t { INIT, WAITING_FOR_NETWORK, NOT_SYNCED, SYNCED };
  SyncState state_{SyncState::INIT};
  bool skip_next_update_{false};

  AttemptResult attempt_once_();

  bool synchronize_epoch_us_(time_t sec, int32_t usec);
  static bool parse_double_(const std::string &s, double &out);
};

}  // namespace httpstime
}  // namespace esphome
