#include "shell/desktop/widgets/desktop_sysmon_widget.h"
#include "tests/test_check.h"

#include <chrono>

class DesktopSysmonWidgetTestAccess {
public:
  static std::chrono::steady_clock::duration
  gaugeUpdateInterval(DesktopSysmonStat stat, const SystemConfig::MonitorConfig& config) {
    return DesktopSysmonWidget::gaugeUpdateInterval(stat, config);
  }

  static std::chrono::milliseconds nextUpdateDelay(
      std::chrono::steady_clock::time_point now, std::chrono::steady_clock::time_point latestSampleAt,
      std::chrono::steady_clock::duration sampleInterval, bool alreadyRetried
  ) {
    return DesktopSysmonWidget::nextUpdateDelay(now, latestSampleAt, sampleInterval, alreadyRetried);
  }
};

int main() {
  using namespace std::chrono_literals;

  SystemConfig::MonitorConfig config;
  config.cpuPollSeconds = 2.0F;
  config.gpuPollSeconds = 3.0F;
  config.memoryPollSeconds = 4.0F;
  config.diskPollSeconds = 5.0F;
  config.networkPollSeconds = 0.5F;

  const auto intervalFor = [&config](DesktopSysmonStat stat) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        DesktopSysmonWidgetTestAccess::gaugeUpdateInterval(stat, config)
    );
  };
  TEST_CHECK(intervalFor(DesktopSysmonStat::CpuUsage) == 2s);
  TEST_CHECK(intervalFor(DesktopSysmonStat::CpuTemp) == 2s);
  TEST_CHECK(intervalFor(DesktopSysmonStat::CpuFreq) == 2s);
  TEST_CHECK(intervalFor(DesktopSysmonStat::GpuUsage) == 3s);
  TEST_CHECK(intervalFor(DesktopSysmonStat::RamPct) == 4s);
  TEST_CHECK(intervalFor(DesktopSysmonStat::SwapPct) == 5s);
  TEST_CHECK(intervalFor(DesktopSysmonStat::NetRx) == 500ms);

  config.cpuPollSeconds = 0.0F;
  TEST_CHECK(intervalFor(DesktopSysmonStat::CpuUsage) == 0ms);

  const auto now = std::chrono::steady_clock::time_point{10s};
  const auto onTimeSample = std::chrono::steady_clock::time_point{9s};
  TEST_CHECK(DesktopSysmonWidgetTestAccess::nextUpdateDelay(now, onTimeSample, 2s, false) == 1020ms);

  const auto staleSample = std::chrono::steady_clock::time_point{7s};
  TEST_CHECK(DesktopSysmonWidgetTestAccess::nextUpdateDelay(now, staleSample, 2s, false) == 25ms);
  TEST_CHECK(DesktopSysmonWidgetTestAccess::nextUpdateDelay(now, staleSample, 2s, true) == 2s);

  const auto noSample = std::chrono::steady_clock::time_point{};
  TEST_CHECK(DesktopSysmonWidgetTestAccess::nextUpdateDelay(now, noSample, 2s, false) == 250ms);
  TEST_CHECK(DesktopSysmonWidgetTestAccess::nextUpdateDelay(now, noSample, 2s, true) == 2s);

  return 0;
}
