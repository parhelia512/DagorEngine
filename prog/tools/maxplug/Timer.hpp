#pragma once
#include <chrono>

class Timer
{
public:
  static double NowMs()
  {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() /
           1000000.;
  }
};

enum class TimerIntervalType
{
  GATHER,
  ACC
};
class TimerInterval
{
  double start, &elapsed;
  TimerIntervalType type;

public:
  TimerInterval(double &elapsed, TimerIntervalType type = TimerIntervalType::ACC) : elapsed(elapsed), type(type)
  {
    start = Timer::NowMs();
    if (type != TimerIntervalType::ACC)
      elapsed = 0.;
  }
  ~TimerInterval()
  {
    if (type == TimerIntervalType::ACC)
      elapsed += Timer::NowMs() - start;
    else
      elapsed = Timer::NowMs() - start;
  }
};
