// SPDX-License-Identifier: MIT
// A miniature flight-software style pipeline:
//   IMU thread  --TripleBuffer-->  control loop  --SpscRing-->  telemetry logger
//   control loop --Seqlock-->      any number of monitors (status snapshot)
//   worker pool  <--MpmcQueue--    control loop (offloaded jobs)
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include "lfc/lfc.hpp"

namespace {

struct ImuSample {
    std::uint64_t sequence = 0;
    double roll = 0;
    double pitch = 0;
    double yaw = 0;
};

struct TelemetryRecord {
    std::uint64_t tick = 0;
    std::uint64_t imu_sequence = 0;
    double command = 0;
};

struct Status {
    std::uint64_t tick = 0;
    double last_command = 0;
};

}  // namespace

int main() {
    using namespace std::chrono_literals;
    constexpr std::uint64_t kTicks = 2000;

    lfc::TripleBuffer<ImuSample> imu_channel;
    lfc::SpscRing<TelemetryRecord> telemetry(256);
    lfc::Seqlock<Status> status;
    lfc::MpmcQueue<std::uint64_t> jobs(64);
    std::atomic<bool> running{true};

    // Sensor: publishes as fast as it can; the controller only wants the latest.
    std::thread imu([&] {
        ImuSample sample;
        while (running.load(std::memory_order_relaxed)) {
            ++sample.sequence;
            const auto t = static_cast<double>(sample.sequence) * 1e-3;
            sample.roll = std::sin(t);
            sample.pitch = std::cos(t);
            sample.yaw = t;
            imu_channel.write(sample);
            std::this_thread::sleep_for(50us);
        }
    });

    // Logger: drains telemetry without ever blocking the control loop.
    std::uint64_t logged = 0;
    std::thread logger([&] {
        TelemetryRecord record;
        while (running.load(std::memory_order_relaxed) || !telemetry.empty_approx()) {
            if (telemetry.try_pop(record)) {
                ++logged;
            } else {
                std::this_thread::sleep_for(100us);
            }
        }
    });

    // Worker pool: processes jobs handed off by the control loop.
    std::atomic<std::uint64_t> jobs_done{0};
    std::vector<std::thread> workers;
    for (int i = 0; i < 2; ++i) {
        workers.emplace_back([&] {
            std::uint64_t job = 0;
            while (running.load(std::memory_order_relaxed) || !jobs.empty_approx()) {
                if (jobs.try_pop(job)) {
                    jobs_done.fetch_add(1, std::memory_order_relaxed);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    // Control loop: fixed-rate, never blocks on any other thread.
    std::uint64_t dropped_telemetry = 0;
    for (std::uint64_t tick = 1; tick <= kTicks; ++tick) {
        const ImuSample& imu_now = imu_channel.read();
        const double command = -0.8 * imu_now.roll - 0.2 * imu_now.pitch;
        if (!telemetry.try_push(TelemetryRecord{tick, imu_now.sequence, command})) {
            ++dropped_telemetry;  // logger fell behind: drop rather than stall
        }
        status.store(Status{tick, command});
        if (tick % 10 == 0 && !jobs.try_push(tick)) {
            ++dropped_telemetry;
        }
        std::this_thread::sleep_for(100us);
    }
    running.store(false);
    imu.join();
    logger.join();
    for (auto& w : workers) {
        w.join();
    }

    const Status final_status = status.load();
    std::printf("control ticks   : %llu\n", static_cast<unsigned long long>(final_status.tick));
    std::printf("telemetry logged: %llu (dropped %llu)\n", static_cast<unsigned long long>(logged),
                static_cast<unsigned long long>(dropped_telemetry));
    std::printf("jobs processed  : %llu\n",
                static_cast<unsigned long long>(jobs_done.load()));
    return final_status.tick == kTicks ? 0 : 1;
}
