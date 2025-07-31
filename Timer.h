#ifndef _TIMER_H_
#define _TIMER_H_
#include <functional>
class Timer {
public:
  //定时器任务类型
  typedef std::function<void()> CallBack_;
  typedef enum{
    TIMER_ONCE=0,//一次触发性定时器
    TIMER_PERIOD//周期性定时器
  }TimerType;
  //超时时间，单位ms
  int timeout_;
  //定时器类型
  TimerType type_;
  //回调函数
  CallBack_ cb_;
  //定时器剩下的转数
  int rotation;
  //定时器所在的槽
  int timeslot;
  //定时器链表指针
  Timer *prev;
  Timer *next;

  Timer(int timeout, TimerType type, const CallBack_ &cb);
  ~Timer();
  void Start();
  //定时器撤销，从管理器中删除
  void Stop();
  //重新设置定时器
  void Adjust(int timeout, TimerType type, const CallBack_ &cb);    
};
#endif // !_TIMER_H_
