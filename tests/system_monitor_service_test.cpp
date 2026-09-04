#include "config/config_types.h"
#include "system/cpu_freq.h"
#include "system/system_monitor_service.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <print>
#include <string>
#include <thread>

namespace {

  std::atomic<int> g_cpuFreqReads{0};

} // namespace

namespace noctalia::system::cpu_freq {

  CpuFreqs readFreqs(const std::filesystem::path&) {
    g_cpuFreqReads.fetch_add(1, std::memory_order_relaxed);
    return {.curMhz = 2400.0, .maxMhz = 4800.0};
  }

} // namespace noctalia::system::cpu_freq

namespace {

  int g_failures = 0;

  void expect(bool condition, const std::string& message) {
    if (!condition) {
      std::println(stderr, "system_monitor_service_test: FAIL: {}", message);
      ++g_failures;
    }
  }

  void testDiskSnapshot(SystemMonitorService& monitor) {
    const std::string path = std::filesystem::temp_directory_path().string();
    monitor.retainDiskPath(path);

    const auto disk = monitor.diskStats(path);
    expect(disk.has_value(), "a valid path should produce a disk snapshot");
    if (disk.has_value()) {
      expect(disk->totalBytes > 0, "disk total should be populated");
      expect(disk->freeBytes <= disk->totalBytes, "disk free bytes should not exceed the total");
      expect(disk->availableBytes <= disk->freeBytes, "available bytes should not exceed free bytes");
      expect(disk->usagePercent >= 0.0f && disk->usagePercent <= 100.0f, "disk usage should be a percentage");
    }
    monitor.releaseDiskPath(path);

    const std::string missingPath = path + "/definitely-not-a-noctalia-filesystem";
    monitor.retainDiskPath(missingPath);
    expect(!monitor.diskStats(missingPath).has_value(), "an unavailable path should not produce a zero snapshot");
    monitor.releaseDiskPath(missingPath);
  }

  void testSampleTimestamp(SystemMonitorService& monitor) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    SystemStats stats;
    do {
      stats = monitor.latest();
      if (stats.sampledAtWall != std::chrono::system_clock::time_point{}) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    } while (std::chrono::steady_clock::now() < deadline);

    expect(stats.sampledAtWall != std::chrono::system_clock::time_point{}, "a completed sample should carry wall time");
    expect(stats.sampledAtWall <= std::chrono::system_clock::now(), "sample wall time should not be in the future");
  }

  void testCpuFreqDemand(SystemMonitorService& monitor) {
    expect(g_cpuFreqReads.load(std::memory_order_relaxed) == 0, "CPU frequency should not be read without a consumer");

    monitor.retainCpuFreq();
    monitor.retainCpuFreq();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    SystemStats stats;
    do {
      stats = monitor.latest();
      if (stats.cpuFreqAvailable) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    } while (std::chrono::steady_clock::now() < deadline);

    expect(stats.cpuFreqAvailable, "retaining CPU frequency should trigger a sample");
    expect(stats.cpuFreqMhz == 2400.0, "the sampled CPU frequency should be published");
    expect(stats.cpuMaxFreqMhz == 4800.0, "the sampled maximum CPU frequency should be published");
    monitor.releaseCpuFreq();

    const int readsBeforeSharedRelease = g_cpuFreqReads.load(std::memory_order_relaxed);
    const auto sharedDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
    while (g_cpuFreqReads.load(std::memory_order_relaxed) == readsBeforeSharedRelease
           && std::chrono::steady_clock::now() < sharedDeadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    expect(
        g_cpuFreqReads.load(std::memory_order_relaxed) > readsBeforeSharedRelease,
        "CPU frequency sampling should continue while one consumer remains"
    );

    monitor.releaseCpuFreq();

    const int readsAfterRelease = g_cpuFreqReads.load(std::memory_order_relaxed);
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    expect(
        g_cpuFreqReads.load(std::memory_order_relaxed) == readsAfterRelease,
        "CPU frequency sampling should stop after the last consumer releases it"
    );

    monitor.retainCpuFreq();
    const auto resumeDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (g_cpuFreqReads.load(std::memory_order_relaxed) == readsAfterRelease
           && std::chrono::steady_clock::now() < resumeDeadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    expect(
        g_cpuFreqReads.load(std::memory_order_relaxed) > readsAfterRelease,
        "retaining CPU frequency again should trigger an immediate sample"
    );
    monitor.releaseCpuFreq();
  }

} // namespace

int main() {
  SystemConfig::MonitorConfig config;
  config.cpuPollSeconds = 1.0f;
  config.gpuPollSeconds = 0.0f;
  config.memoryPollSeconds = 0.0f;
  config.networkPollSeconds = 0.0f;
  config.diskPollSeconds = 1.0f;

  SystemMonitorService monitor(config);
  testDiskSnapshot(monitor);
  testSampleTimestamp(monitor);
  testCpuFreqDemand(monitor);
  return g_failures == 0 ? 0 : 1;
}
