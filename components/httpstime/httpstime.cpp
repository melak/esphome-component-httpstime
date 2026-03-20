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

#include "httpstime.h"
#include <cmath>
#include <cstdlib>
#include <cerrno>
#include <sys/time.h>
#include "esphome/core/log.h"
#include "esphome/components/network/util.h"

#include "vtim.h"

namespace esphome {
namespace httpstime {

static const char *const TAG = "httpstime";

static constexpr uint32_t NETWORK_WAIT_POLL_MS = 1 * 1000;
static constexpr uint32_t INITIAL_SYNC_RETRY_MS = 15 * 1000;

static bool network_ready() {
#ifdef USE_NETWORK
  if (network::is_connected()) {
    return true;
  }
  auto ips = network::get_ip_addresses();
  for (auto ip : ips) {
    if (ip.is_set()) {
      return true;
    }
  }
  return false;
#else
  return true;
#endif
}

void HTTPSTimeComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "HTTPSTIME Time Source:");
  ESP_LOGCONFIG(TAG, " URL: %s", url_.c_str());
  if(steady_interval_captured_) {
    ESP_LOGCONFIG(TAG, " Update interval: %zu ms", steady_interval_ms_);
  } else {
    LOG_UPDATE_INTERVAL(this);
  }
  time::RealTimeClock::dump_config();
}

bool HTTPSTimeComponent::parse_double_(const std::string &s, double &out) {
  errno = 0;
  char *end = nullptr;
  out = std::strtod(s.c_str(), &end);
  if (end == s.c_str() || *end != '\0' || errno == ERANGE) {
    return false;
  }
  return std::isfinite(out);
}

bool HTTPSTimeComponent::synchronize_epoch_us_(time_t sec, int32_t usec) {
  struct timeval tv{.tv_sec = sec, .tv_usec = usec};
  struct timezone tz = {0, 0};
  int ret = settimeofday(&tv, &tz);
  if (ret == EINVAL) {
    ret = settimeofday(&tv, nullptr);
  }
  auto t = this->now();
  if (!t.is_valid()) {
    return false;
  }
  this->time_sync_callback_.call();
  return true;
}

void HTTPSTimeComponent::call_setup() {
  time::RealTimeClock::call_setup();
  steady_interval_ms_ = this->get_update_interval();
  steady_interval_captured_ = true;
  this->stop_poller();
  this->set_update_interval(NETWORK_WAIT_POLL_MS);
  this->defer([this]() { this->update(); });
}

HTTPSTimeComponent::AttemptResult HTTPSTimeComponent::attempt_once_() {
  std::vector<http_request::Header> headers;
  auto warm = this->parent_->get(url_, headers);
  if (!warm) {
    return AttemptResult::NET_FAIL;
  }
  warm->end();
  yield();

  std::vector<std::string> collect_headers = { "date", "x-httpstime" };
  auto req = this->parent_->get(url_, headers, collect_headers);
  if (!req) {
    return AttemptResult::NET_FAIL;
  }

  const uint32_t rtt_ms = req->duration_ms;
  std::string hdr_date = req->get_response_header("date");
  std::string hdr_xhttpstime = req->get_response_header("X-HTTPSTIME");
  req->end();

  double server_epoch = std::numeric_limits<double>::quiet_NaN();
  if (!hdr_xhttpstime.empty()) {
    if (!parse_double_(hdr_xhttpstime, server_epoch)) {
        return AttemptResult::NO_TIME;
    }
  } else {
    server_epoch = VTIM_parse(hdr_date.c_str());
  }

  if (!std::isfinite(server_epoch) || server_epoch < 0.0) {
    return AttemptResult::NO_TIME;
  }

  const double adjusted = server_epoch + (rtt_ms / 1000.0) * 0.5;
  double sec_d;
  double frac = std::modf(adjusted, &sec_d);
  time_t sec = static_cast<time_t>(sec_d);
  int32_t usec = static_cast<int32_t>(llround(frac * 1e6));

  if (!this->synchronize_epoch_us_(sec, usec)) {
    return AttemptResult::NO_TIME;
  }

  return AttemptResult::TIME_SET;
}

void HTTPSTimeComponent::update() {
  if (!this->parent_ || url_.empty()) {
    return;
  }

  auto enter_synced = [this]() {
    this->cancel_timeout("httpstime_wait4_network");
    this->cancel_timeout("httpstime_initial_retry");

    if (steady_interval_captured_ && this->get_update_interval() != steady_interval_ms_) {
      this->set_update_interval(steady_interval_ms_);
    }
    this->start_poller();
    this->skip_next_update_ = true;
  };

  for (;;) {
    const SyncState cur = this->state_;
    const bool have_time = this->now().is_valid();
    const bool network_ready = esphome::network::is_connected();

    if (have_time && cur == SyncState::NOT_SYNCED) {
      this->state_ = SyncState::SYNCED;
      continue;
    }

    switch (cur) {
      case SyncState::INIT: {
        if (have_time) {
          this->state_ = SyncState::SYNCED;
          enter_synced();
          return;
        }
        if (network_ready) {
          this->state_ = SyncState::NOT_SYNCED;
          continue;
        }
        this->state_ = SyncState::WAITING_FOR_NETWORK;
        continue;
      }

      case SyncState::WAITING_FOR_NETWORK: {
        if (have_time) {
          this->state_ = SyncState::SYNCED;
          enter_synced();
          return;
        }
        if (network_ready) {
          this->state_ = SyncState::NOT_SYNCED;
          continue;
        }
        this->set_timeout("httpstime_wait4_network", NETWORK_WAIT_POLL_MS, [this]() { this->update(); });
        return;
      }

      case SyncState::NOT_SYNCED: {
        if (!network_ready) {
          this->state_ = SyncState::WAITING_FOR_NETWORK;
          continue;
        }

        auto res = attempt_once_();
        if (res == AttemptResult::TIME_SET) {
          this->state_ = SyncState::SYNCED;
          enter_synced();
          return;
        }

        this->set_timeout("httpstime_initial_retry", INITIAL_SYNC_RETRY_MS, [this]() { this->update(); });
        return;
      }

      case SyncState::SYNCED: {
        if (!network_ready) {
          return;
        }

        if (this->skip_next_update_) {
          this->skip_next_update_ = false;
          return;
        }

        (void) attempt_once_();
        return;
      }
    }
  }
}

} // namespace httpstime
} // namespace esphome
