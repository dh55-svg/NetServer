#include <iostream>
#include <thread>
#include <ctime>
#include <ratio>
#include <chrono>
#include <unistd.h>
#include <sys/time.h>
#include "TimerManager.h"
TimerManager *TimerManager::instance_ = nullptr;
std::mutex TimerManager::mutex_;
TimerManager::GC TimerManager::gc;
const int TimerManager::slotinterval = 1; // 每个slot的时间间隔，单位ms
const int TimerManager::slotnum = 1024; // 时间轮的slot数量
TimerManager::TimerManager() : currentslot(0), running_(false),timewheel(slotnum, nullptr),th_() {
    // 初始化时间轮
}
TimerManager::~TimerManager() {
    Stop();
}
TimerManager* TimerManager::GetInstance() {
    if (instance_ == nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (instance_ == nullptr) {
            instance_ = new TimerManager();
        }
    }
    return instance_;
}
void TimerManager::AddTimer(Timer* ptimer) {
  if(ptimer == nullptr) return;
    std::lock_guard<std::mutex> lock(wheelmutex_);
    CalculateTimer(ptimer);
    AddTimerToTimeWheel(ptimer);
}
void TimerManager::RemoveTimer(Timer* ptimer) {
    if(ptimer == nullptr) return;
    std::lock_guard<std::mutex> lock(wheelmutex_);
    RemoveTimerFromTimeWheel(ptimer);
}
void TimerManager::AdjustTimer(Timer* ptimer) {
  if (ptimer == nullptr) return;
  std::lock_guard<std::mutex> lock(wheelmutex_);
  AdjustTimerToWheel(ptimer);
}
/**
 * @brief 计算定时器在时间轮中的位置参数（轮次和槽位）
 * @param ptimer 待计算的定时器指针，需包含有效的timeout_成员
 * @details 根据定时器的超时时间、时间轮槽位间隔及总槽数，计算定时器应放置的时间轮槽位和需要经过的轮次
 *          确保定时器在正确的时间点被触发
 */
void TimerManager::CalculateTimer(Timer* ptimer) {
    if (ptimer == nullptr) return; // 空指针检查，避免无效操作
    int tick = 0;                  // 定时器需要经过的时间槽间隔数（ticks）
    int timeout = ptimer->timeout_; // 获取定时器的超时时间（单位：ms）

    // 若超时时间小于单个槽位间隔，向上取整为1个tick（至少经过1个槽位间隔）
    if (timeout < slotinterval) {
        tick = 1;
    } else {
        // 否则计算超时时间包含多少个完整的槽位间隔（整数除法）
        tick = timeout / slotinterval;
    }

    // 计算定时器需要经过的时间轮完整轮次（总ticks / 时间轮总槽数）
    ptimer->rotation = tick / slotnum;
    // 计算定时器应放置的目标槽位：(当前槽位 + 总ticks)取模总槽数，确保在有效范围
    int timeslot = (currentslot + tick) % slotnum;
    ptimer->timeslot = timeslot; // 保存计算得到的目标槽位
}
void TimerManager::AddTimerToTimeWheel(Timer* ptimer)
{
    if(ptimer == nullptr)
        return;

    int timeslot = ptimer->timeslot;

    if(timewheel[timeslot])
    {
        ptimer->next = timewheel[timeslot];    
        timewheel[timeslot]->prev = ptimer;
        timewheel[timeslot] = ptimer;
    }
    else
    {
        timewheel[timeslot] = ptimer;
    }
}

void TimerManager::RemoveTimerFromTimeWheel(Timer* ptimer)
{
    if(ptimer == nullptr)
        return;

    int timeslot = ptimer->timeslot;

    if(ptimer == timewheel[timeslot])
    {
        //头结点
        timewheel[timeslot] = ptimer->next;
        if(ptimer->next != nullptr)
        {
            ptimer->next->prev = nullptr;
        }
        ptimer->prev = ptimer->next = nullptr;
    }
    else
    {
        if(ptimer->prev == nullptr) //不在时间轮的链表中，即已经被删除了
            return;
        ptimer->prev->next = ptimer->next;
        if(ptimer->next != nullptr)
            ptimer->next->prev = ptimer->prev;
        
        ptimer->prev = ptimer->next = nullptr;
    }    
}

void TimerManager::AdjustTimerToWheel(Timer* ptimer)
{
    if(ptimer == nullptr)
        return;

    RemoveTimerFromTimeWheel(ptimer);
    CalculateTimer(ptimer);
    AddTimerToTimeWheel(ptimer);
}

void TimerManager::CheckTimeout()//执行当前slot的任务
{
    std::lock_guard<std::mutex> lock(wheelmutex_);
    Timer *ptimer = timewheel[currentslot];
    while(ptimer != nullptr)
    {        
        if(ptimer->rotation > 0)
        {
            --ptimer->rotation;
            ptimer = ptimer->next;
        }
        else
        {
            //可执行定时器任务
            ptimer->cb_(); //注意：任务里不能把定时器自身给清理掉！！！我认为应该先移除再执行任务
            if(ptimer->type_ == Timer::TimerType::TIMER_ONCE)
            {
                Timer *ptemptimer = ptimer;
                ptimer = ptimer->next;
                RemoveTimerFromTimeWheel(ptemptimer);
            }
            else
            {
                Timer *ptemptimer = ptimer;
                ptimer = ptimer->next;
                AdjustTimerToWheel(ptemptimer);
                if(currentslot == ptemptimer->timeslot && ptemptimer->rotation > 0)
                {
                    //如果定时器多于一转的话，需要先对rotation减1，否则会等待两个周期
                    --ptemptimer->rotation;
                }
            }            
        }        
    }
    currentslot = (++currentslot) % TimerManager::slotnum; //移动至下一个时间槽
}

void TimerManager::CheckTick()
{
    // steady_clock::time_point t1 = steady_clock::now();
    // steady_clock::time_point t2 = steady_clock::now();
    // duration<double> time_span;
    int si = TimerManager::slotinterval;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int oldtime = (tv.tv_sec % 10000) * 1000 + tv.tv_usec / 1000;
    int time;
    int tickcount;
    while(running_)
    {
        gettimeofday(&tv, NULL);
        time = (tv.tv_sec % 10000) * 1000 + tv.tv_usec / 1000;
        tickcount = (time - oldtime)/slotinterval; //计算两次check的时间间隔占多少个slot
        //oldtime = time;
        oldtime = oldtime + tickcount*slotinterval;

        for(int i = 0; i < tickcount; ++i)
        {
            TimerManager::GetInstance()->CheckTimeout();
        }        
        std::this_thread::sleep_for(std::chrono::microseconds(500)); //milliseconds(si)时间粒度越小，延时越不精确，因为上下文切换耗时
        // t2 = steady_clock::now();
        // time_span = duration_cast<duration<double>>(t2 - t1);
        // t1 = t2;
        // std::cout << "thread " << time_span.count() << " seconds." << std::endl;
    }
}

void TimerManager::Start()
{
    running_ = true;
    th_ = std::thread(&TimerManager::CheckTick, this);
}

void TimerManager::Stop()
{
    running_ = false;
    if(th_.joinable())
        th_.join();
}