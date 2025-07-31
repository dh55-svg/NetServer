#include "Timer.h"
#include <sys/time.h>
#include "TimerManager.h"
Timer::Timer(int timeout, TimerType type, const CallBack_ &cb)
    : timeout_(timeout), type_(type), cb_(cb), rotation(0), timeslot(0), prev(nullptr), next(nullptr) {
      if(timeout<0) return; //不合法的定时器
}
Timer::~Timer() {
    //析构函数不做任何操作，定时器的删除由TimerManager管理
    Stop();
}
void Timer::Start() {
    TimerManager::GetInstance()->AddTimer(this);
}
void Timer::Stop() {
    TimerManager::GetInstance()->RemoveTimer(this);
}
void Timer::Adjust(int timeout, TimerType type, const CallBack_ &cb) {
    timeout_ = timeout;
    type_ = type;
    cb_ = cb;
    TimerManager::GetInstance()->AdjustTimer(this);
}
